// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct {
  struct spinlock lock;  //这个表示每个桶的锁
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;     //
} bcache[BCACHE_CNT];//开多个桶
struct spinlock steal_block;  //偷窃锁，定义一个锁去保证寻找，驱逐，插入的原子性

void
binit(void)
{

  initlock(&steal_block, "steal_block");//给偷窃锁初始化
  for (int i = 0; i < BCACHE_CNT; i++)                    
  {
      initlock(&bcache[i].lock, "bcache");//给每个桶锁初始化
      bcache[i].head.next = &bcache[i].head; //给每个桶的链表初始化
  }
  
  //接下来可以将所有的缓存块全部放入0号桶中，他们会自己调整的
  struct buf *b;
  // Create linked list of buffers
  for(b = bcache[0].buf; b < bcache[0].buf + NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    b->refcnt = 0;                     //基本信息
    b->lastuse = 0;
    b->next = bcache[0].head.next;    //插入链表头
    bcache[0].head.next = b;
    
  }
}

//从桶id的链表中删除last_used这个节点
int erase(int id, struct buf *last_used )
{
  struct buf *b;
  for(b = &bcache[id].head; b->next != &bcache[id].head; b = b->next){
    if(b->next == last_used){      //找到他的上一个节点
      b->next = b->next->next; 
      return 0; 
    }
  }
  return -1;
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  //得到在那个桶中
  uint blnum = BUFMAP_HASH(dev, blockno);

  //先获取锁，查找当前桶内有无catch到
  acquire(&bcache[blnum].lock);
  struct buf *b;
  // Is the block already cached?是否cached
  for(b = bcache[blnum].head.next; b != &bcache[blnum].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache[blnum].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  //没有catched，这里先释放自身的锁，再获取偷窃锁
  release(&bcache[blnum].lock);    //先释放自身的锁
  acquire(&steal_block);           //获取偷窃锁
  //这里再次判断是否catch，防止给一个块分配不同缓存的问题。
  for(b = bcache[blnum].head.next; b != &bcache[blnum].head; b = b->next){
   if(b->dev == dev && b->blockno == blockno){
      acquire(&bcache[blnum].lock);                //为什么在这里才加锁，注意到我们修改缓存的设备号和块号只在窃取其他空闲块的时候才窃取
      b->refcnt++;                                 //而以上过程只在获取steal_block锁之后才可以发生，但由于此时已经获取了锁，所以不必担心错误问题。
      release(&bcache[blnum].lock);                //而这里添加锁是因为要修改引用次数，所以加锁。
      release(&steal_block);
      acquiresleep(&b->lock);
      return b;
    }
  }
  
  //窃取其他桶的块，这里包括当前桶，因为要从所有桶中找到最近未使用的锁
  struct buf *last_used = 0;
  int last_id = -1;
  for (int i = 0; i < BCACHE_CNT; i++)           //枚举所有桶
  {
    acquire(&bcache[i].lock);                    //获取桶的锁
    int new_found = 0;
    for(b = bcache[i].head.next; b != &bcache[i].head; b = b->next)
    {
      if(b->refcnt == 0 && (last_used == 0 || last_used->lastuse > b->lastuse))//如果b是未使用的，
      {
        new_found = 1;
        last_used = b;
      }                    
    }
    //当前桶有没有更小的，直接释放当前桶的锁
    if(!new_found) release(&bcache[i].lock);
    else   //否则，先释放之前桶的锁，更改id
    {
      if(last_id != -1)
        release(&bcache[last_id].lock);
      last_id = i;
    }
  }
  
  //其他桶也满了，报错
  if(last_id == -1) 
    panic("bget: no buffers");

  //接下来，将找到的缓存块从原本的桶的链表中去除
  if(erase(last_id, last_used) < 0)
    panic("bget:erase failed");
  //窃取的缓存块已经从原链表删除了，可以释放他的锁了。
  release(&bcache[last_id].lock);

  //将窃取的块加入当前桶中
  acquire(&bcache[blnum].lock);//首先获取锁
  last_used->next = bcache[blnum].head.next;
  bcache[blnum].head.next = last_used;
  //更新该块的其他信息
  last_used->dev = dev;
  last_used->blockno = blockno;       //此时是更新缓存的块号和设备号的唯一时机，和上方注释照应。
  last_used->valid = 0;
  last_used->refcnt = 1;
  //释放锁.
  release(&bcache[blnum].lock); //释放当前桶的锁
  release(&steal_block);        //释放偷窃锁
  acquiresleep(&last_used->lock);
  return last_used;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint key = BUFMAP_HASH(b->dev, b->blockno);
  acquire(&bcache[key].lock);
  b->refcnt--;
  if (b->refcnt == 0) 
    b->lastuse = ticks;   //没引用标记说明已经是空闲缓存，标记此时的时间
  
  release(&bcache[key].lock);
}

void
bpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);
  acquire(&bcache[key].lock);
  b->refcnt++;
  release(&bcache[key].lock);
}

void
bunpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);
  acquire(&bcache[key].lock);
  b->refcnt--;
  release(&bcache[key].lock);
}