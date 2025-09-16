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
  struct run *st_buf[STEAL_CNT];   //建立缓冲区
} kmem[NCPU];                 //每个cpu一个

void
kinit()
{
  for (int i = 0; i < NCPU; i++)
  {
    initlock(&kmem[NCPU].lock, "kmem");
  }
  
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

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  push_off();
  uint64 cid = cpuid();
  pop_off();

  acquire(&kmem[cid].lock);
  r->next = kmem[cid].freelist;
  kmem[cid].freelist = r;
  release(&kmem[cid].lock);
}

//cid窃取其他cpu核的函数
uint64 steal(uint64 cid)
{
  uint64 st_tot = 0;                              //该变量表示窃取的个数
  for (int i = 0; i < NCPU; i++)                  //枚举所有的cpu核
  {
    if(i == cid) continue;                        //若是当前的cid，跳过
    acquire(&kmem[i].lock);                       //获取锁，因为要操作frelist
    while(st_tot < STEAL_CNT && kmem[i].freelist) //当前窃取的不够且当前cpu核还有剩余
    {
      kmem[cid].st_buf[st_tot++] = kmem[i].freelist; //赋给缓冲区，
      kmem[i].freelist = kmem[i].freelist->next;   //被窃取的cpu核内存下一个
    }
    release(&kmem[i].lock);                        //释放锁
    if(st_tot == STEAL_CNT) return STEAL_CNT;      ///窃取够了，直接润
  }

  return st_tot;                                   //返回窃取个数
  
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  push_off();                  //先关中断，获取当前cpu的id
  uint64 cid = cpuid();

  //开始处理内存
  acquire(&kmem[cid].lock);
  r = kmem[cid].freelist;
  if(r) {                      //该核还有内存，直接分配，释放锁即可
    kmem[cid].freelist = r->next;
    release(&kmem[cid].lock);
  }
  else {                        //没有内存
    release(&kmem[cid].lock);   //首先释放该核的锁
    uint64 st_cnt = steal(cid); //窃取其他核的内存
    if(st_cnt == 0)             //内存全满了，返回0
    {
      pop_off();                //不要忘了关中断
      return 0;
    }
    acquire(&kmem[cid].lock);   //上锁，将缓冲区的内存放入该核内存中
    for (int i = 0; i < st_cnt; i++)
    {
      kmem[cid].st_buf[i] ->next = kmem[cid].freelist;
      kmem[cid].freelist = kmem[cid].st_buf[i];
    }
    r = kmem[cid].freelist;     //取出r方便之后返回
    kmem[cid].freelist = r->next;
    release(&kmem[cid].lock);   //释放锁   
  }

  pop_off(); //关中断

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk

  return (void*)r;
}
