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

// reference count for pages
struct refc {
  struct spinlock lock;
  int page[PHYSTOP/PGSIZE];
} refCount;

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
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    acquire(&refCount.lock);
    refCount.page[(p - ((char *) PGROUNDUP((uint64) end))) / PGSIZE] = 1;
    release(&refCount.lock);
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  acquire(&refCount.lock);
  refCount.page[(((char *) pa) - ((char *) PGROUNDUP((uint64) end))) / PGSIZE]--;
  release(&refCount.lock);

  acquire(&refCount.lock);
  int refCNT = refCount.page[(((char *) pa) - ((char *) PGROUNDUP((uint64) end))) / PGSIZE];
  release(&refCount.lock);

  if (refCNT > 0) return;

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

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk

    acquire(&refCount.lock);
    int refCNT = refCount.page[(((char *) r) - ((char *) PGROUNDUP((uint64) end))) / PGSIZE];
    release(&refCount.lock);

    if (refCNT != 0) {
      printf("refCount is %d\n", refCount.page[(((char *) r) - ((char *) PGROUNDUP((uint64) end))) / PGSIZE]);
      panic("kalloc: refCount not 0");
    }
    acquire(&refCount.lock);
    refCount.page[(((char *) r) - ((char *) PGROUNDUP((uint64) end))) / PGSIZE]++;
    release(&refCount.lock);
  }  

  return (void*)r;
}
