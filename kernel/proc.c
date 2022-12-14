#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

//Newly added for setting seed for returning random number
int seed = 37;

//Newly added for MLFQ scheduling
int ticks_allowed[5] = {1,2,4,8,16}; //ticks allowed for each queue

struct proc *queue0[NPROC];   //Making 5 queues for MLFQ
struct proc *queue1[NPROC];
struct proc *queue2[NPROC];
struct proc *queue3[NPROC];
struct proc *queue4[NPROC];


int queue0_ptr = -1;     //Pointers for each queue
int queue1_ptr = -1;
int queue2_ptr = -1;
int queue3_ptr = -1;
int queue4_ptr = -1;


struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;
  p->creationTime= ticks;   //newly added for FCFS scheduling
  p->st_time=0;  //newly added for PBS scheduling start time
  p->stat_pr=60; //newly added for PBS scheduling static priority
  p->run_time=0; //newly added for PBS scheduling run time
  p->sleep_time=0; //newly added for PBS scheduling sleep time
  p->numOfScheduled=0; //newly added for PBS and FCFS scheduling 
  p->tickets=1; //newly added for LBS scheduling for number of tickets

  p->curr_queue=0;  //Current queue of the process at the start will be zero
  queue0_ptr++; //Increase pointer for queue0
  queue0[queue0_ptr] = p; //newly added for MLFQ scheduling. Put the first process in queue0
  p->num_of_ticks[0]=0; //Newly added initialized the number of ticks of the process
  p->num_of_ticks[1]=0;
  p->num_of_ticks[2]=0;
  p->num_of_ticks[3]=0;
  p->num_of_ticks[4]=0;

  p->wait_time=3;

  

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  if((p->trapframe_copy = (struct trapframe *)kalloc()) == 0){
      release(&p->lock);
      return 0;
  }

  p->is_sigalarm=0;
  p->tickss=0;
  p->now_ticks=0;
  p->handler=0;
  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe_copy)
    kfree((void*)p->trapframe_copy);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  //copying trace mask from parent to child process  (newly added)
  np->mask = p->mask;

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

//Newly added to return a random number
int
getrand(int tot_tickets)
{
  int nxt_seed = (seed*seed)%100;
  seed = nxt_seed;
  int rand = (int)((nxt_seed*1.0)/99)*tot_tickets;
  return rand;
}

//Newly added to remove a process from the queue
void
remove_from_queue(int pid, int curr_queue)
{
  if(curr_queue == 0){
    for(int i=0; i<=queue0_ptr; i++){
      if(queue0[i]->pid == pid){
        for(int j=i; j<=queue0_ptr-1; j++){
          queue0[j] = queue0[j+1];
        }
        queue0_ptr--;
        break;
      }
    }
  }
  else if(curr_queue == 1){
    for(int i=0; i<=queue1_ptr; i++){
      if(queue1[i]->pid == pid){
        for(int j=i; j<=queue1_ptr-1; j++){
          queue1[j] = queue1[j+1];
        }
        queue1_ptr--;
        break;
      }
    }
  }
  else if(curr_queue == 2){
    for(int i=0; i<=queue2_ptr; i++){
      if(queue2[i]->pid == pid){
        for(int j=i; j<=queue2_ptr-1; j++){
          queue2[j] = queue2[j+1];
        }
        queue2_ptr--;
        break;
      }
    }
  }
  else if(curr_queue == 3){
    for(int i=0; i<=queue3_ptr; i++){
      if(queue3[i]->pid == pid){
        for(int j=i; j<=queue3_ptr-1; j++){
          queue3[j] = queue3[j+1];
        }
        queue3_ptr--;
        break;
      }
    }
  }
  else{
    for(int i=0; i<=queue4_ptr; i++){
      if(queue4[i]->pid == pid){
        for(int j=i; j<=queue4_ptr-1; j++){
          queue4[j] = queue4[j+1];
        }
        queue4_ptr--;
        break;
      }
    }
  }
}

//Ageing added in scheduler function 

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  //struct proc *p;  moved it in default scheduler
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    #ifdef RR  //Added newly for default scheduler
    struct proc *p;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      //This is the default scheduling algorithm
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
    #else
    
    #ifdef MLFQ
    //struct proc *p=0;
    //acquire(&p->lock);
    struct proc *process=0;

    //--------------------AGEING STARTED--------------------
    //Checking for a upgradation process in queue1
  for(int i=0;i<=queue1_ptr;i++){
    if(queue1[i]->state == RUNNABLE){
      if(ticks - queue1[i]->st_time > queue1[i]->wait_time){
        queue1[i]->curr_queue--;
        int cqueue=queue1[i]->curr_queue;
        queue1[i]->num_of_ticks[cqueue] = 0;

        queue1[i]->st_time=ticks;

        queue0_ptr++;         //Incrementing the pointer of queue0
        queue0[queue0_ptr] = queue1[i];   //Adding the process to queue0
        queue1_ptr--;         //Decrementing the pointer of queue1 hence logically removing the process from queue1
        remove_from_queue(queue1[i]->pid, 1);
      }
    }
  }

  //Checking for a upgradation process in queue2
  for(int i=0;i<=queue2_ptr;i++){
    if(queue2[i]->state == RUNNABLE){
      if(ticks - queue2[i]->st_time > queue2[i]->wait_time){
        queue2[i]->curr_queue--;
        int cqueue=queue2[i]->curr_queue;
        queue2[i]->num_of_ticks[cqueue] = 0;

        queue2[i]->st_time=ticks;

        queue1_ptr++;         //Incrementing the pointer of queue1
        queue1[queue1_ptr] = queue2[i];   //Adding the process to queue1
        queue2_ptr--;         //Decrementing the pointer of queue1 hence logically removing the process from queue1
        remove_from_queue(queue2[i]->pid, 2);
      }
    }
  }

  //Checking for a upgradation process in queue3
  for(int i=0;i<=queue3_ptr;i++){
    if(queue3[i]->state == RUNNABLE){
      if(ticks - queue3[i]->st_time > queue3[i]->wait_time){
        queue3[i]->curr_queue--;
        int cqueue=queue3[i]->curr_queue;
        queue3[i]->num_of_ticks[cqueue] = 0;

        queue3[i]->st_time=ticks;

        queue2_ptr++;         //Incrementing the pointer of queue1
        queue2[queue2_ptr] = queue3[i];   //Adding the process to queue1
        queue3_ptr--;         //Decrementing the pointer of queue1 hence logically removing the process from queue1
        remove_from_queue(queue3[i]->pid, 3);
      }
    }
  }

  //Checking for a upgradation process in queue4
  for(int i=0;i<=queue4_ptr;i++){
    if(queue4[i]->state == RUNNABLE){
      if(ticks - queue4[i]->st_time > queue4[i]->wait_time){
        queue4[i]->curr_queue--;
        int cqueue=queue4[i]->curr_queue;
        queue4[i]->num_of_ticks[cqueue] = 0;

        queue4[i]->st_time=ticks;

        queue3_ptr++;         //Incrementing the pointer of queue1
        queue3[queue3_ptr] = queue4[i];   //Adding the process to queue1
        queue4_ptr--;         //Decrementing the pointer of queue1 hence logically removing the process from queue1
        remove_from_queue(queue4[i]->pid, 4);
      }
    }
  }


    //--------------------AGEING ENDED--------------------

    //Newly added first checking queue0 for process
    if(queue0_ptr >=0){
      for(int i=0;i<=queue0_ptr;i++){
        acquire(&queue0[i]->lock);
        if(queue0[i]->state == RUNNABLE){
          queue0[i]->st_time=ticks;
          process=queue0[i];

          c->proc = process;
          process->state=RUNNING;
          process->numOfScheduled++;
          swtch(&c->context, &process->context);

          c->proc = 0;

          //If time has expired
          if(process->num_of_ticks[0] >= ticks_allowed[0]){
            if(!process->killed){
              process->curr_queue++;
              queue1_ptr++;
              queue1[queue1_ptr]=process;
            }
          }

          for(int j=i;j<=queue0_ptr;j++){
            queue0[j]=queue0[j+1];
          }

          queue0_ptr--;
          process->num_of_ticks[0]=0;
        }
        release(&queue0[i]->lock);
      }
    }

    //Newly added first checking queue1 for process
    if(queue1_ptr >=0){
      for(int i=0;i<=queue1_ptr;i++){
        acquire(&queue1[i]->lock);  
        if(queue1[i]->state == RUNNABLE){
          queue1[i]->st_time=ticks;
          process=queue1[i];

          c->proc = process;
          process->state=RUNNING;
          process->numOfScheduled++;
          swtch(&c->context, &process->context);

          c->proc = 0;

          //If time has expired
          if(process->num_of_ticks[1] >= ticks_allowed[1]){
            if(!process->killed){
              process->curr_queue++;
              queue2_ptr++;
              queue2[queue2_ptr]=process;
            }
          }

          for(int j=i;j<=queue1_ptr;j++){
            queue1[j]=queue1[j+1];
          }

          queue1_ptr--;
          process->num_of_ticks[1]=0;
        }
        release(&queue1[i]->lock);
      }
    }

    //Newly added first checking queue2 for process
    if(queue2_ptr >=0){
      for(int i=0;i<=queue2_ptr;i++){
        acquire(&queue2[i]->lock);
        if(queue2[i]->state == RUNNABLE){
          queue2[i]->st_time=ticks;
          process=queue2[i];

          c->proc = process;
          process->state=RUNNING;
          process->numOfScheduled++;
          swtch(&c->context, &process->context);

          c->proc = 0;

          //If time has expired
          if(process->num_of_ticks[2] >= ticks_allowed[2]){
            if(!process->killed){
              process->curr_queue++;
              queue3_ptr++;
              queue3[queue3_ptr]=process;
            }
          }

          for(int j=i;j<=queue2_ptr;j++){
            queue2[j]=queue2[j+1];
          }

          queue2_ptr--;
          process->num_of_ticks[2]=0;
        }
        release(&queue2[i]->lock);
      }
    }

    //Newly added first checking queue3 for process
    if(queue3_ptr >=0){
      for(int i=0;i<=queue3_ptr;i++){
        acquire(&queue3[i]->lock);
        if(queue3[i]->state == RUNNABLE){
          queue3[i]->st_time=ticks;
          process=queue3[i];

          c->proc = process;
          process->state=RUNNING;
          process->numOfScheduled++;
          swtch(&c->context, &process->context);

          c->proc = 0;

          //If time has expired
          if(process->num_of_ticks[3] >= ticks_allowed[3]){
            process->curr_queue++;
            queue4_ptr++;
            queue4[queue4_ptr]=process;
          }

          for(int j=i;j<=queue3_ptr;j++){
            queue3[j]=queue3[j+1];
          }

          queue3_ptr--;
          process->num_of_ticks[3]=0;
        }
        release(&queue3[i]->lock);
      }
    }

    //Newly added first checking queue4 for process
    if(queue4_ptr >=0){
      for(int i=0;i<=queue4_ptr;i++){
        acquire(&queue4[i]->lock);
        if(queue4[i]->state == RUNNABLE){
          queue4[i]->st_time=ticks;
          process=queue4[i];

          c->proc = process;
          process->state=RUNNING;
          process->numOfScheduled++;
          swtch(&c->context, &process->context);

          c->proc = 0;

          if(!process->killed){
            for(int j=i;j<queue4_ptr;j++){
              queue4[j]=queue4[j+1];
            }
            queue4[queue4_ptr]=process;
          }
          else{
            for(int j=i;j<queue4_ptr;j++){
              queue4[j]=queue4[j+1];
            }
          }
        }
        release(&queue4[i]->lock);
      }
    }
    
    //release(&p->lock);
    #else
    #ifdef FCFS
    struct proc* newProcess;
    struct proc* startProcess = 0;
    for (newProcess = proc; newProcess < &proc[NPROC]; newProcess++)
      {
        acquire(&newProcess->lock);
        if (newProcess->state == RUNNABLE)
        {
          if (startProcess == 0 || newProcess->creationTime < startProcess->creationTime)
            {
                if (startProcess !=0)
                    release(&startProcess->lock);

                startProcess = newProcess;
                continue;
            }
        }
        release(&newProcess->lock);
      }

      if (startProcess)
      {
        startProcess->state = RUNNING;
        
        c->proc = startProcess;
        newProcess->numOfScheduled++;
        swtch(&c->context, &startProcess->context);

        c->proc = 0;
        release(&startProcess->lock);
      }
    #else
    #ifdef PBS
    struct proc* p;
    struct proc* process = 0;
    int dynamic_pr = 999;
    for (p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);

      int niceness = 5;

      if (p->numOfScheduled > 0 && (p->sleep_time + p->run_time != 0))
      {
        niceness = (p->sleep_time / (p->sleep_time + p->run_time)) * 10;
      }
      else{
        niceness = 5;
      }

      int val = p->stat_pr - niceness + 5;

      int tmp=-1;
      if(val < 100){
        tmp=val;
      }
      else{
        tmp=100;
      }

      int processDp=-1;
      if(0 > tmp){
        processDp=0;
      }
      else{
        processDp=tmp;
      }

      int check_cond_1 = (dynamic_pr == processDp && p->numOfScheduled < process->numOfScheduled);
      int check_cond_2 = check_cond_1 && (p->creationTime < process->creationTime);

      if (p->state == RUNNABLE)
      {
        if(!process || dynamic_pr > processDp || check_cond_1 || check_cond_2)
        {
          if (process)
            release(&process->lock);

          dynamic_pr = processDp;
          process = p;
          
          continue;
        }
      }
      release(&p->lock);
    }

    if (process)
    {
      process->run_time = 0;
      process->sleep_time = 0;
      process->numOfScheduled++;
      process->state = RUNNING;
      process->st_time = ticks;
      
      c->proc = process;
      swtch(&c->context, &process->context);
      c->proc = 0;
      release(&process->lock);
    }
    #else

    #ifdef LBS
    struct proc* p;
    struct proc* process = 0;
    int tot_tickets = 0,num_passed_tickets = 0;
    for (p = proc; p < &proc[NPROC]; p++){
      if(p->state == RUNNABLE){
        tot_tickets += p->tickets;
      }
    }

    int ans = getrand(tot_tickets);
    for (p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      if(p->state == RUNNABLE){
        num_passed_tickets += p->tickets;
        if(num_passed_tickets > ans){
          process=p;
          break;
        }

      }
      release(&p->lock);
    }

    if(process){
      process->state = RUNNING;
        
      c->proc = process;
      swtch(&c->context, &process->context);

      c->proc = 0;
      release(&process->lock);
    }


    #endif
    #endif
    #endif
    #endif
    #endif
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

int setPriority(int priority, int pid)
{
    struct proc *p;

    for(p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      
      if(p->pid == pid)
      {
        int val = p->stat_pr;
        p->stat_pr = priority;

        p->run_time = 0;
        p->sleep_time = 0;

        release(&p->lock);

        if (val > priority)
            yield();
        return val;
      }
      release(&p->lock);
    }
    return -1;
}

void
updateTime()
{
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);

    if (p->state == RUNNING)
    {
      p->run_time++;
      p->tot_run_time++;
    }

    if (p->state == SLEEPING)
      p->sleep_time++;

    release(&p->lock);

  }
}

int
waitx(uint64 addr, uint* rtime, uint* wtime)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          *rtime = np->tot_run_time;
          *wtime = np->end_time - (np->creationTime + np->tot_run_time);
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,sizeof(np->xstate)) < 0){
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

uint64 sys_sigalarm(void){
    int ticks;
    argint(0, &ticks);
    uint64 handler;
    argaddr(1, &handler);
    myproc()->is_sigalarm = 0;
    myproc()->tickss = ticks;
    myproc()->now_ticks = 0;
    myproc()->handler = handler;
    return 0;
}