<h1> OSN Assignment-4 <br></h1>
By Ishan Kavathekar (2022121003) and Kunal Bhosikar (2022121005) for CS3.301 Operating Systems and Networks  </br>

# Specifications
## 1. System Calls
### System Call 1 : <code> trace </code> </br>
-  strace runs the specified command until it exits.
- It intercepts and records the system calls which are called by a process during its
execution. 

1. Added <code> $U/_trace</code> in UPROGS to Makefile.  
2. Added <code> sys_trace</code> in <code> syscall.h</code> and <code>syscall.c</code> for mapping it to a number and making it extern.   

3. Implemented <code>sys_trace</code> in <code> kernel/sysproc.c</code>
```c
uint64
sys_trace(void)
{
  int mask;

  argint(0,&mask);
  myproc()-> mask = mask;
  return 0;
}
```
4. Added <code> strace.c</code> in <code>user</code> while actually executes the trace system call.
```c
int main(int argc, char *argv[]) 
{
    int max_commands=32;
    char *new_argv[max_commands];

    if(argc < 3 || argc > max_commands)
    {
        fprintf(2, "Enter valid range of arguments");
        exit(1);
    }
    else{
        int check=trace(atoi(argv[1]));
        if (check < 0) 
        {
            fprintf(2, "%s: strace  command failed\n", argv[0]);
            exit(1);
        }
        else{
            for(int i = 2; i < argc && i < max_commands; i++)
    	    new_argv[i-2] = argv[i];

            exec(new_argv[0], new_argv);
            exit(0);
        }
    }
    
}
```
5. Modified <code>syscall()</code> in <code> kernel/syscall.c</code> to print the output. All the information is stored in an array.

```c
void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;

  //ADDED NEWLY
  int length=syscallnum[num];
  int collec_syscall_names[length];

  for(int i=0;i< length;i++){
    collec_syscall_names[i]=argraw(i);
  }
  //NEWLY ADDED PART ENDED

  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // Use num to lookup the system call function for num, call it,
    // and store its return value in p->trapframe->a0
    p->trapframe->a0 = syscalls[num]();
    
    //ADDED NEWLY
    if(p->mask & (1<<num)){
      printf("%d: syscall %s (",p->pid,syscall_names[num]);
      for(int i=0;i<length;i++){
        printf("%d ",collec_syscall_names[i]);
      }
      printf("\b) -> %d\n",argraw(0));
      
    }
    //ENDED THE NEWLY ADDED PART
  } else {
    printf("%d %s: unknown sys call %d\n",p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
```
6. Initialsed <code> int mask</code> in <code>kernel/proc.h</code> and modified <code> fork.c</code> to o copy the trace mask from the parent to the child process.

7. Made an entry in <code>usys.pl</code> with repect to the system call.

### System Call 1 : <code>sigalarm</code> and <code>sigreturn</code>

In this system call, we'll add a feature to xv6 that periodically alerts a process as it uses CPU time.

1. Add two syscall <code>sigalarm</code> and <code>sigreturn</code>
    1. We declare the system calls in <code>syscall.h</code> and <code>syscall.c</code>.
       1. <code>syscall.c</code>
       ```c
       extern uint64 sys_sigalarm(void);
       extern uint64 sys_sigreturn(void);
       
       
       [SYS_sigalarm]   sys_sigalarm,
       [SYS_sigreturn]   sys_sigreturn,
       ```
       2. <code>syscall.h</code>
       ```c
       #define SYS_sigalarm  22
       #define SYS_sigreturn  23
       ```
    2. Added the system calls to <code>user.h</code> and <code>usys.S</code>.
       1. <code>user.h</code>
       ```c
       int sigalarm(int ticks, void (*handler)(void));
       int sigreturn(void);
       ```
       2. <code>usys.S</code>
       ```c
       SYSCALL(sigalarm)
       SYSCALL(sigreturn)
       ```
    3. We add new fields to <code>proc</code> structure.
       1. <code>proc.h</code>
       ```c
          int is_sigalarm;
          int ticks;
          int now_ticks;
          uint64 handler;
          struct trapframe *trapframe_copy;
       ```
    Since we need to mark how many ticks have passed we declare <code>now_ticks</code> and we use <code>kicks</code> to store the value passed by syscall and we use <code>handler</code> to store the handler function address. The most hard part to understand is why we need a new <code>trapframe</code> structure. I think we can think this way, once the handler function has expired time interupt can still occur and in this way we store the variables at that time(when executing the handler function), so the variables we store to trapframe when we first expire the handler function are overwritten so we need a new trapframe to store the registers when first expire handler function.
    4. Initialize the variables and withdraw them.
       1. <code>proc.c</code>
       ```c
       allocproc:
         if((p->trapframe_copy = (struct trapframe *)kalloc()) == 0){
           release(&p->lock);
           return 0;
         }

         p->is_sigalarm=0;
         p->ticks=0;
         p->now_ticks=0;
         p->handler=0;


       freeproc:
         if(p->trapframe_copy)
           kfree((void*)p->trapframe_copy);
            p->trapframe = 0;

       uint64 sys_sigalarm(void){
         int ticks;
         if(argint(0, &ticks) < 0)
           return -1;
         uint64 handler;
         if(argaddr(1, &handler) < 0)
           return -1;
         myproc()->is_sigalarm =0;
         myproc()->ticks = ticks;
         myproc()->now_ticks = 0;
         myproc()->handler = handler;
         return 0;
       ```
    5. If the process has a timer outstanding then expire the handler function.
       1. <code>trap.c</code>
        ```c
          if(which_dev == 2){
            p->now_ticks+=1;
            if(p->ticks>0&&p->now_ticks>=p->ticks&&!p->is_sigalarm){
              p->now_ticks = 0;
              p->is_sigalarm=1;
              *(p->trapframe_copy)=*(p->trapframe);
              p->trapframe->epc=p->handler;
            }
          yield();
          }
        ```
    6. Design the return function
       1. <code>sysproc.c</code>
       ```c
       int
       sys_sigalarm(void)
       {
         int ticks;
         void (*handler)(void);
       
         if(argint(0, &ticks) < 0 || argptr(1, (void*)&handler, sizeof(handler)) < 0)
           return -1;
         return sigalarm(ticks, handler);
       }
       
       int
       sys_sigreturn(void)
       {
         return sigreturn();
       }
       ```

## 2. Scheduling
- The default scheduler of xv6 is round-robin-based.
- In this task, we have implemented First Come First Serve(FCFS), Lottery Based Scheduling(LBS), Priority Based Scheduling(PBS) and Multilevel Feedback Queue(MLFQ).

### First Come First Serve(FCFS)
1. Added <code>creationTime</code> and <code>numOfScheduled</code> which is the creation time of the process and number of scheduled processes respectively in <code> kernel/proc.c</code>

2. Initialized <code>creationTime</code> and <numOfScheduled> to zero in <code>allocproc()</code> in <code>kernel/proc.c</code>

3. Implemented the First Come First Serve(FCFS) scheduling in <code>scheduler()</code> function in <code>kernel/proc.c</code>
```c
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

                firstProcess = newProcess;
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
```
4. Disabled preemption in <code>user/trap.c</code> by adding conditions to <code>yield()</code>.

### Lottery Based Scheduling(LBS)

1. Added <code>tickets</code> in <code>kernel/proc.h</code>.
2. Initialsed <code>tickets</code> it to one in <code>allocproc()</code> function in <code>kernel/proc.c</code>

3. Created a <code>getrand()</code> function in <code>kernel/proc.c</code>
to get a random number between the range 0 and total number of tickets.

```c
int
getrand(int tot_tickets)
{
  int nxt_seed = (seed*seed)%100;
  seed = nxt_seed;
  int rand = (int)((nxt_seed*1.0)/99)*tot_tickets;
  return rand;
}
```

4. Implemented Lottery Based Scheduling in <code>scheduler()</code> function in <code>kernel/proc.c</code>

```c
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
```

### Priority Based Scheduling(PBS)

1. Added <code>stat_pr</code>, <code>st_time</code>, <code>sleep_time</code> and <code>run_time</code>which is the static priority, start time and sleep time of a process in <code>kernel/proc.h</code>

2. Initialsed <code>stat_pr</code> to 60 in <code>allocproc()</code> in <code>kernel/proc.h</code>

3. Added <code>setPriority</code> systemcall in <code>syscall.h</code> and <code>syscall.h</code>.

4. Added <code>setPriority.c</code> in <code>user</code>

```c
#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc,char *argv[])
{
    if(argc < 3 || argc > 3)
    {
        fprintf(2, "Enter valid number of arguments\n", argv[0]);
        exit(1);
    }

    int pid = atoi(argv[2]);
    int priority = atoi(argv[1]);

    if(priority<0 || priority>100)
    {
        fprintf(2, "Priority must be between 0 and 100\n");
        exit(1);
    }
    setPriority(priority,pid);
    exit(1);
}
```
5. Implemented Priority Based Scheduling in <code> scheduler()</code> function in <code> kernel/proc.c</code>

```c
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
```

### Multi Level Feedback Queue ( MLFQ )

1. Implemente a preemptive MLFQ using 5 arrays for each of the queue
```c
struct proc *queue0[NPROC];   
struct proc *queue1[NPROC];
struct proc *queue2[NPROC];
struct proc *queue3[NPROC];
struct proc *queue4[NPROC];
```
2. If the process takes longer time than the given time slice, then the process is shifted to lower queue.

3. Ageing is implemeted where a process is shifted to an upper queue if it has stayed in a lower queue for a long period of time.

4. <code>remove_from_queue()</code> in <code>kernel/proc.c</code>function is used to remove a particular process from any given queue.

### Testing
A new file <code>user/schedulertest.c</code> was used to test the scheduling algorithms

#### Round Robin
Average rtime=22     
wtime=166

#### FCFS
Average rtime=48    
wtime=129

#### LBS
Average rtime=25     
wtime=79

#### PBS
Average rtime=23     
wtime=67