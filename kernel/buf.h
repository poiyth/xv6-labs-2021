struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  // struct buf *prev; // LRU cache list   由于增加了lastuse字样，所以不需要prev了，单向链表足以
  struct buf *next;
  uint lastuse;
  uchar data[BSIZE];
};

