#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "file.h"
#include "fcntl.h"


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

  for(int i = 0; i < NOFILE; i++) {
    p->vmas[i].state = UNUSED;
    p->vmas[i].addr = 0;
    p->vmas[i].index = 0;
    p->vmas[i].file = 0;
    p->vmas[i].len = 0;
    p->vmas[i].prot = 0;
    p->vmas[i].flags = 0;
    p->vmas[i].fd = 0;
    p->vmas[i].offset = 0;
  };

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

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
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



/*
int *vma_alloc(…)
vma_copy(int *vma) - call fork copy memcopy or memmove on vma struc
vma_dealloc(int *vma) -> if unmap start of vma
vma_includes(int *vma)
vma_adjust(int *vma, …) - 
vma_print(int *vma, …)

always want to keep start of vma
*/


uint64
min(uint64 a, uint64 b){
  if (a > b){
    return b;
  }
  if (b > a){
    return a;
  }
  return a;
}


int
vma_print(struct vma *vma){
  printf("VMA: \n STATE: %d\n ADDR:  %lu \n i: %d\n LEN: %lu\n PROT: %d\n FLAGS: %d\n  FD: %d\n OFFSET: %lu\n \n", 
    (int)vma->state, vma->addr, vma->index, vma->len, vma->prot, vma->flags, vma->fd, vma->offset);
  return 0;
}


int
vma_includes(struct vma *v, uint64 va){ //pointer when passing in structure as argument
  //printf("\nVMA_includes\n");
  //vma_print(v);
  
  if (v->state == USED){
    if (v->addr <= va && (v->addr + v->len) > va){
        return 0;
    }
  }
  return -1;
}

struct vma *
find_vma(struct proc *p, uint64 va){

  for(int i=0; i < NOFILE; i++){
    if (vma_includes(&p->vmas[i], va) == 0){
      //printf("\nfind_VMA \n");
      //vma_print(&p->vmas[i]);
      return &p->vmas[i];
    }
  }
  return 0;
}


int
vma_dealloc(struct vma *vma, size_t len, uint64 addr, struct proc *p)
{
  if(vma != 0){

  //if munmap removes all pages of a previous mmap, it should decrement the reference count 
  //of the corresponding struct file. 

    if (len == vma->len){

      fileclose(vma->file);

      vma->state = UNUSED;
      vma->top = 0;
      vma->addr = 0;
      vma->index = 0;
      vma->file = 0;
      vma->len = 0;
      vma->prot = 0;
      vma->flags = 0;
      vma->fd = 0;
      vma->offset = 0;

      p->sz = p->sz-len;

      return 0;
    }

    if (vma->addr == addr){ //top
      vma->addr = vma->addr + len;
      vma->offset = vma->offset + len;
      vma->len = (vma->len - len);
      return 0;
    }

    if(vma->addr < addr){ //bottom
      vma->len = (vma->len - len);
      return 0;
    }

  }
  return -1;
}


int
vma_alloc(struct proc *p)
{
  for(int i = 0; i < NOFILE; i++) {
    if(p->vmas[i].state == UNUSED){
      p->vmas[i].state = USED;
      return i;
    }
  }
  return -1;
}


uint64
proc_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset){
  struct proc *p = myproc();

// find an unused region in the process's address 
//space in which to map the file

  int index = vma_alloc(p);

  if (index != -1){
    //find an unused region in the process's address space in which to map the file
    uint64 region = PGROUNDUP(p->sz); //grabbing the very top of the next spot in the heap then setting the new top of the heap to account for the size of the mapping
    p->sz = region + len;

//VMA should contain a pointer to a struct file for the file being mapped
    //mmap should increase the file's reference count so that the structure 
    //doesn't disappear when the file is closed (hint: see filedup)    

    if ((flags & MAP_PRIVATE) == 0){
      if (((prot & PROT_WRITE) != 0) && ((p->ofile[fd]->writable) == 0)){
        return -1;
      }
    }


    p->vmas[index].addr = region;
    p->vmas[index].top = region;
    p->vmas[index].index = index;
    p->vmas[index].file = p->ofile[fd];
    p->vmas[index].len = len;
    p->vmas[index].prot = prot;
    p->vmas[index].flags = flags;
    p->vmas[index].fd = fd;
    p->vmas[index].offset = offset;
    filedup(p->vmas[index].file);
    return p->vmas[index].addr;
  }
    //Run mmaptest: the first mmap should succeed, but the first access to 
    //the mmap-ed memory will cause a page fault and kill mmaptest.
  
  return 0xffffffffffffffff;
}

uint64
proc_munmap(void *addr, size_t len){

  printf("input - addr: %lu, len: %lu \n", (uint64)addr, len);

  struct proc *p = myproc();

  struct vma *vma = find_vma(p, (uint64)addr);

  printf("reached munmap\n");
  

  if (vma != 0){

      //If an unmapped page has been modified and the file is mapped MAP_SHARED, 
      //write the page back to the file. Look at filewrite for inspiration.

      if ((vma->flags & MAP_SHARED) != 0){

        int off = min((uint64)len,  min((uint64)vma->len, (uint64)(vma->file->ip->size - vma->offset)));
        filewrite(vma->file, vma->addr, off);
        
        // ilock(vma->file->ip);    
        // writei(vma->file->ip, 1, vma->addr, vma->offset, off);
        // iunlock(vma->file->ip);
      }

  //find the VMA for the address range and unmap the specified pages (hint: use uvmunmap).

    for(uint64 i = (uint64)addr; i < ((uint64)addr + len); i+=PGSIZE){

      //printf("i: %lu\n", i);

      pte_t *pte = walk(p->pagetable, PGROUNDDOWN(i), 0);
          if (pte != 0){
            if((*pte & PTE_V) != 0){
              //printf("passed check\n");
              uvmunmap(p->pagetable, PGROUNDDOWN(i), 1, 0);
            }
          }
    }
    vma_dealloc(vma, len, (uint64)addr, p);
      return 0;
    }
  return -1;
}


int
mmap_fault_handler(struct proc *p, uint64 va){


  //printf("va: %lu \n", va);

  int flags;
  pagetable_t page = p->pagetable;

  pte_t *pte = walk(page, PGROUNDDOWN(va), 0);

  if(va >= MAXVA){
      return -1;
  }

  if (pte != 0){
    if(*pte & PTE_V){
      return -1;
    }
  }

  struct vma *vma = find_vma(p, va);

  if (vma != 0){ //condition checks if va falls anywhere within the mapping
      char *pa = kalloc();

      if (pa == 0){
        return -1;
      }

      //printf("kalloc: %s\n", pa);

      memset(pa, 0, PGSIZE); //clean the page
      struct file *file = vma->file;
      uint64 off = va - vma->addr;

      ilock(file->ip);      
      readi(file->ip, 0, (uint64)pa, (off + vma->offset), PGSIZE);
      iunlock(file->ip);

      //convert prot to flags mappages knows (perm) PTE_R PTE_W only check for w, always want v and r

      if((vma->prot & PROT_WRITE) != 0){
        flags = (PTE_U | PTE_R | PTE_V | PTE_W);
      }
      else{
        flags = (PTE_U | PTE_R | PTE_V);
      }

      mappages(page, PGROUNDDOWN(va), PGSIZE, (uint64)pa, flags);

      //printf("mappages: %d\n", a);

      return 0;
    }
    return -1;
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

  //printf("before p->sz: %lu\n", p->sz);
  printf("exit\n");

  for(int i = 0; i < NOFILE; i++){
    if (p->vmas[i].state == UNUSED){
      continue;
    }

    uint64 addr = p->vmas[i].addr;
    uint64 len  = p->vmas[i].len;

    proc_munmap((void *)addr, len);
  }

  //printf("after p->sz: %lu\n", p->sz);

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
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting.
    intr_on();

    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
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
        found = 1;
      }
      release(&p->lock);
    }
    if(found == 0) {
      // nothing to run; stop running on this core until an interrupt.
      intr_on();
      asm volatile("wfi");
    }
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
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();
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






