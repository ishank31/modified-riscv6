#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// Fetch the uint64 at addr from the current process.
int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  if(addr >= p->sz || addr+sizeof(uint64) > p->sz) // both tests needed, in case of overflow
    return -1;
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
int
fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();
  if(copyinstr(p->pagetable, buf, addr, max) < 0)
    return -1;
  return strlen(buf);
}

static uint64
argraw(int n)
{
  struct proc *p = myproc();
  switch (n) {
  case 0:
    return p->trapframe->a0;
  case 1:
    return p->trapframe->a1;
  case 2:
    return p->trapframe->a2;
  case 3:
    return p->trapframe->a3;
  case 4:
    return p->trapframe->a4;
  case 5:
    return p->trapframe->a5;
  }
  panic("argraw");
  return -1;
}

// Fetch the nth 32-bit system call argument.
void
argint(int n, int *ip)
{
  *ip = argraw(n);
}

// Retrieve an argument as a pointer.
// Doesn't check for legality, since
// copyin/copyout will do that.
void
argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);
}

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  argaddr(n, &addr);
  return fetchstr(addr, buf, max);
}

// Prototypes for the functions that handle system calls.
extern uint64 sys_fork(void);
extern uint64 sys_exit(void);
extern uint64 sys_wait(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_kill(void);
extern uint64 sys_exec(void);
extern uint64 sys_fstat(void);
extern uint64 sys_chdir(void);
extern uint64 sys_dup(void);
extern uint64 sys_getpid(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_sleep(void);
extern uint64 sys_uptime(void);
extern uint64 sys_open(void);
extern uint64 sys_write(void);
extern uint64 sys_mknod(void);
extern uint64 sys_unlink(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_close(void);
extern uint64 sys_trace(void); //Newly added. This is the prototype for the new system call strace.
extern uint64 sys_setPriority(void); //Newly added. This is the prototype for the new system call setPriority.
extern uint64 sys_settickets(void); //Newly added. This is the prototype for the new system call settickets.
extern uint64 sys_waitx(void); //Newly added. This is the prototype for the new system call waitx. 
extern uint64 sys_sigalarm(void); //Newly added. This is the prototype for the new system call sigalarm.
extern uint64 sys_sigreturn(void); //Newly added. This is the prototype for the new system call sigreturn.

// An array mapping syscall numbers from syscall.h
// to the function that handles the system call.
static uint64 (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
[SYS_trace]  sys_trace, //This is a pointer to the function which will actually execute 
[SYS_setPriority] sys_setPriority,  //This is pointer to the function will actually execute the setPriority system call.
[SYS_settickets] sys_settickets, //This is pointer to the function will actually execute the settickets system call.
[SYS_waitx] sys_waitx, //This is pointer to the function will actually execute the waitx system call.
[SYS_sigalarm] sys_sigalarm, //This is pointer to the function will actually execute the sigalarm system call.
[SYS_sigreturn] sys_sigreturn, //This is pointer to the function will actually execute the sigreturn system call.
};

//ADDED NEWLY
static char *syscall_names[] = {
[SYS_fork]    "fork",
[SYS_exit]    "exit",
[SYS_wait]    "wait",
[SYS_pipe]    "pipe",
[SYS_read]    "read",
[SYS_kill]    "kill",
[SYS_exec]    "exec",
[SYS_fstat]   "fstat",
[SYS_chdir]   "chdir",
[SYS_dup]     "dup",
[SYS_getpid]  "getpid",
[SYS_sbrk]    "sbrk",
[SYS_sleep]   "sleep",
[SYS_uptime]  "uptime",
[SYS_open]    "open",
[SYS_write]   "write",
[SYS_mknod]   "mknod",
[SYS_unlink]  "unlink",
[SYS_link]    "link",
[SYS_mkdir]   "mkdir",
[SYS_close]   "close",
[SYS_trace]   "trace",  //newly added for strace system call
[SYS_setPriority] "setPriority", //newly added for setPriority system call
[SYS_settickets] "settickets", //newly added for settickets system call
[SYS_waitx] "waitx", //newly added for waitx system call
};

static int syscallnum[] = {
[SYS_fork] 0,
[SYS_exit] 1,
[SYS_wait] 1,
[SYS_pipe] 0,
[SYS_read] 3,
[SYS_kill] 2,
[SYS_exec] 2,
[SYS_fstat] 1,
[SYS_chdir] 1,
[SYS_dup] 1,
[SYS_getpid] 0,
[SYS_sbrk] 1,
[SYS_sleep] 1,
[SYS_uptime] 0,
[SYS_open] 2,
[SYS_write] 3,
[SYS_mknod] 3,
[SYS_unlink] 1,
[SYS_link] 2,
[SYS_mkdir] 1,
[SYS_close] 1,
[SYS_trace] 1,
[SYS_setPriority] 2,
[SYS_settickets] 2,
[SYS_waitx] 3,
[SYS_sigalarm] 2,
[SYS_sigreturn] 0,
};
//ENDED THE NEWLY ADDED PART

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
