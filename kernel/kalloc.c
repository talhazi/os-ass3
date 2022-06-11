// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

int references[NUM_PYS_PAGES];
struct spinlock r_lock;

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

// Used for Copy-On-Write
extern uint64 cas(volatile void *addr, int expected, int newval);



struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;



int 
reference_find(uint64 pa)
{
  return references[NUM_PYS_PAGES_PA(pa)];
}

int
reference_add(uint64 pa)
{
  int ref;
  do{
      ref = references[NUM_PYS_PAGES_PA(pa)];
  } while(cas(&references[NUM_PYS_PAGES_PA(pa)] ,ref,ref+1));
  return ref;
}

int
reference_remove(uint64 pa)
{
  int ref;
  do{
      ref = references[NUM_PYS_PAGES_PA(pa)];
  } while(cas(&references[NUM_PYS_PAGES_PA(pa)] ,ref,ref-1));
  return ref;
}



void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&r_lock, "refereces");
  memset(references, 0, sizeof(int)*NUM_PYS_PAGES);
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

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
  int ref = reference_find((uint64)pa);

  if (ref > 1){
    reference_remove((uint64)pa);
    return;
  }

  references[NUM_PYS_PAGES_PA((uint64)pa)] = 0;
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;

  if(r) {
    references[NUM_PYS_PAGES_PA((uint64)r)] = 1;
    kmem.freelist = r->next;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
