#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  uint64 va_addr, mask_addr;
  int pg_size;

  //读取三个参数
  argaddr(0, &va_addr);
  argint(1, &pg_size);
  argaddr(2, &mask_addr);

  //mask只有32位，这里需要判断是否超出限制
  if(pg_size > 32) return -1;

  uint64 addr = va_addr;
  uint mask = 0;
  pagetable_t pagetable = myproc()->pagetable;

  //接下来访问虚拟地址页面有无被访问过
  for(int i = 0; i < pg_size; i++)
  {
    pte_t *pte = walk(pagetable, addr, 0);
    if(pte && ((*pte) & PTE_V) && ((*pte) & PTE_A))
    {
      mask |= 1LL<<i;
      (*pte) &= (~PTE_A);           //清除该pte上的1.
    }
    addr += PGSIZE;
  }

  copyout(pagetable, mask_addr, (char *)&mask, sizeof(mask));

  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

