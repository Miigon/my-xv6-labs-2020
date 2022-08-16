struct buf {
  // trash: this buf contains invalid dev and blockno
  // and needs to be evicted and re-hashed before use.
  int trash;
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  uint lastuse;
  struct buf *next;
  uchar data[BSIZE];
};

