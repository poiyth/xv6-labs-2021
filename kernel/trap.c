#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fcntl.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//返回该地址属于哪个vma，没有返回-1
int get_mmap_index(struct proc *p, uint64 addr)
{
  int i;
  for(i = 0; i < VMA_SZ; i++)   //遍历所有的vma，查看是否在其中
  {
    if(addr >= p->mmap_vmas[i].addr && addr < p->mmap_vmas[i].addr + p->mmap_vmas[i].length)
      return i;
  }
  return -1;
}

int mmap_fault_handler(struct proc *p, uint64 addr)
{
  //首先判断权限对不对
  int index = get_mmap_index(p, addr);               
  if((r_scause() == 13 && !(p->mmap_vmas[index].prot & PROT_READ)) ||
      (r_scause() == 15 && !(p->mmap_vmas[index].prot & PROT_WRITE))) {
    printf("mmap port filed\n");
    return -1;
  }

  //接着分配一个实际的物理页
  uint64 *pa = kalloc(); 
  if(!pa)
  {
    printf("kalloc failed!\n");
    return -1;
  }  
  memset(pa, 0, PGSIZE);     //物理页清空

  //接下来将文件中的内容搬到此物理页
  addr = PGROUNDDOWN(addr);  //虚拟地址找到页首地址
  uint64 offset = addr - p->mmap_vmas[index].addr; //距离文件的偏移
  ilock(p->mmap_vmas[index].fd->ip);  //要读取文件中的内容，首先要获取该inode节点的锁
  //这点注意，因为可能映射的空间大于文件大小，所以可能读取内容不足一页设置可能不读取（当offset>=文件大小时）
  if(readi(p->mmap_vmas[index].fd->ip, 0, (uint64)pa, offset, PGSIZE) < 0)
  {
    iunlock(p->mmap_vmas[index].fd->ip);
    printf("readi from file filed!\n");
    return -1;
  } 
  iunlock(p->mmap_vmas[index].fd->ip);       //这里inode节点使用完毕需要释放锁

  //最后将虚拟地址和物理地址在页表中映射
  //PTE的权限
  int perm = PTE_U | PTE_V;
  if(p->mmap_vmas[index].prot | PROT_READ) perm |= PTE_R;
  if(p->mmap_vmas[index].prot | PROT_WRITE) perm |= PTE_W;
  if(p->mmap_vmas[index].prot | PROT_EXEC) perm |= PTE_X;
  //将虚拟地址和物理地址进行关联
  if(mappages(p->pagetable, addr, PGSIZE, (uint64)pa, perm) < 0)
  {
    printf("mappages filed\n");
    kfree(pa);
    return -1;
  }

  return 0;
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;
  int bad = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } //这里识别是读取和写页错误，并且是mmap区域的页错误
  else if((r_scause() == 13 || r_scause() == 15) && get_mmap_index(p, r_stval()) != -1)
  {
    // printf("mmaptest!!!!!!!");
    if(mmap_fault_handler(p, r_stval()) < 0) 
      bad = 1;
  }
  else {
    bad = 1;
  }

  if(bad == 1) {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

