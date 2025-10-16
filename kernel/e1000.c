#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));  //发送缓冲区
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16   //缓冲区环的大小
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));  //接受缓冲区
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(struct mbuf *m)
{
  //防止多个cpu抢用，先获取锁
  acquire(&e1000_lock); 

  //拿到尾指针，判断当前环是否合法
  uint32 Tail= regs[E1000_TDT];
  struct tx_desc *desc = &tx_ring[Tail];
  if(!(desc->status & E1000_TXD_STAT_DD))   //尾指针指向的是第一个空闲的，但这里还未处理，说明之前有错误。
  {
    printf("e1000Transmit failed! txring Tail is not DD\n");
    release(&e1000_lock);
    return -1;
  }

  //接下来来了一个新的数据包的buf，这里先判断tail这里是否还有数据包没释放
  if(tx_mbufs[Tail]) //如果有数据包的话，先释放
  {
    mbuffree(tx_mbufs[Tail]); 
    tx_mbufs[Tail] = 0; 
  }
  
  //该描述符挂载的mbuf为空，填充该描述符信息
  desc->addr = (uint64)m->head;  //这里addr指向m中数据开始的地方，也就是head
  desc->length = m->len; //数据长度
  desc->status = 0;      //初始化status
  desc->cmd |= E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS; //设置这两个标志位，一个是包结束的标志，一个是报告状态->当发送完毕后，硬件自动设置DD位
  tx_mbufs[Tail] = m;    //数据包挂载

  //修改尾指针
  regs[E1000_TDT] = (regs[E1000_TDT] + 1) % TX_RING_SIZE;
  //最后释放整个锁
  release(&e1000_lock);
  //添加成功，返回0
  return 0;
}

static void
e1000_recv(void)
{
  //这里不应该加锁
  // acquire(&e1000_lock); 
  //设备的接受程序应该是一个循环，将E1000写入内存的所有数据全部取出交给网络栈分析
  while(1)
  {
    
    //根据提示，获取尾指针
    uint32 Tail = (regs[E1000_RDT] + 1) % RX_RING_SIZE;   //注意这里取的是尾指针下一个
    struct rx_desc *desc = &rx_ring[Tail];
    //如果当前描述符对应的数据没有接收完毕，这里DD的含义都是硬件是否处理完成
    //所以这里DD的含义就是当前数据是否已经接收完毕了
    if(!(desc->status & E1000_RXD_STAT_DD))  break; 
    //这里数据包里有数据，但长度还未设置，但描述符长度是有的
    rx_mbufs[Tail]->len = desc->length;
    net_rx(rx_mbufs[Tail]);    //将数据包送到网络层
    rx_mbufs[Tail] = mbufalloc(0);  //给当前数据缓冲区分配一个新的空数据包
    if (!rx_mbufs[Tail])            //分配空间不足，报错
      panic("e1000 e1000_recv mbufalloc failed!");
      
    desc->addr = (uint64) rx_mbufs[Tail]->head;     //将描述符存放数据地址指向新的数据包的存放数据地方
    desc->status = 0;                               //该描述符数据已经拿走，清空状态
    regs[E1000_RDT] = (regs[E1000_RDT] + 1) % RX_RING_SIZE; //尾指针向后走
  }
  
  // release(&e1000_lock); 这里同理
}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
