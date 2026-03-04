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

static int ref_count [(PHYSTOP-KERNBASE)/PGSIZE]; //cleaner to make get func

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

//my code
void
update_ref_count(uint pa, int sign){
  uint index = (pa-KERNBASE)/PGSIZE;
  if (sign == 1){
  ref_count[index] += 1; 
  }
  else if(sign == 0){
  ref_count[index] -= 1;  
  }
}

int
get_ref_count(uint pa){
  int count = ref_count[(pa-KERNBASE)/PGSIZE];
  return count;
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  
  if(ref_count[((uint64)pa-KERNBASE)/PGSIZE] == 0){
    r->next = kmem.freelist;
    kmem.freelist = r;
  }
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
    update_ref_count((uint64)r, 1);
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
    //increment ref count
  return (void*)r;
}


void
kfree_lock(void *pa)
{
  acquire(&kmem.lock);
  kfree(pa);
  release(&kmem.lock);
}

void *
kalloc_lock(void)
{
  struct run *r;
  acquire(&kmem.lock);
  r = kalloc();
  release(&kmem.lock);
  return (void*)r;
} 


void
check_if_zero(uint64 pa, int sign){
  acquire(&kmem.lock);
  update_ref_count(pa, sign);
  if(get_ref_count(pa) == 0){
    kfree((void *) pa);
  }
  release(&kmem.lock);
}

void
lock_update_ref_count(uint pa, int sign){
    acquire(&kmem.lock);
    update_ref_count(pa,sign);
    release(&kmem.lock);
}


int 
update(pagetable_t table, uint64 va, pte_t *pte, uint64 pa, uint flags){
  uint updated_flags;
  uint64 new_pa;
  acquire(&kmem.lock);
  if(get_ref_count(pa) == 0){
    kfree((void *) pa);
  if(get_ref_count(pa) > 1){
      updated_flags = flags | PTE_W; //update to write
      updated_flags = updated_flags & ~PTE_COW; //remove cow

      new_pa = (uint64)kalloc(); //Allocate a new physical page
      memmove((void *)new_pa, (const void *)pa, PGSIZE); //Copy the contents from the old page into the new one
      mappages(table, va, PGSIZE, new_pa, updated_flags);//Map the new page as writable
      update_ref_count(pa, 0);
  }
    }
    else if(get_ref_count(pa) == 1){
      updated_flags = flags | PTE_W; //update to write
      updated_flags = updated_flags & ~PTE_COW; //remove cow
      mappages(table, va, PGSIZE, pa, updated_flags);
    }
    release(&kmem.lock);
    return 0;
  }