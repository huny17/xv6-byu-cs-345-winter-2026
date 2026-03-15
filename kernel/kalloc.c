// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

/*

Each CPU usually has:

Its own free list

Its own lock

This avoids multiple CPUs fighting over one lock during heavy allocation.

Flow:

CPU0 kalloc -> CPU0 free list
CPU1 kalloc -> CPU1 free list

CPU0 kfree -> CPU0 free list
CPU1 kfree -> CPU1 free list

5. One more detail students often miss

When you determine the CPU (often via a cpuid helper), 
you usually need to disable interrupts temporarily while doing it so the thread doesn’t migrate CPUs mid-operation.


struct cpu*     mycpu(void);
struct cpu*     getmycpu(void);
*/

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)

/*

Determine the current CPU

Insert the freed page into that CPU’s free list

*/


void  //my code
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

/*
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}
*/

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.

/*
Determine the current CPU

Allocate from that CPU’s local free list

If the local list is empty, it may steal/borrow pages 
from another CPU’s list or a global pool

Identify the CPU

Access the correct free list

Possibly rebalance if empty
*/


void * //my code
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
\



/*
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
*/

/*
You can use the constant NCPU from kernel/param.h

Let freerange give all free memory to the CPU running freerange. 
You do *not* need to spread out the blocks up front.

The function cpuid returns the current core number, but it's only 
safe to call it and use its result when interrupts are turned off. 
You should use push_off() and pop_off() to turn interrupts off and 
on. (You can get away with loose consistency in this lab, but the 
practice of disabling interrupts when working with a particular CPU 
is good practice.)

You may need to implement a "steal from largest list" optimization.

  And you can search for the "largest" without locking.

Be careful of deadlock - in particular holding the lock on one free 
list, while acquiring the lock on another.

You should not need to implement a "steal multiple" optimization 
(though you certainly may).

Have a look at the snprintf function in kernel/sprintf.c for string 
formatting ideas. It is OK to just name all locks "kmem" though.

Optionally run your solution using xv6's race detector:
*/