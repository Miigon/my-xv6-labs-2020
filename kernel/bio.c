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

#define NBUFMAP_BUCKET 13
#define BUFMAP_HASH(dev, blockno) ((((dev)<<27)|(blockno))%NBUFMAP_BUCKET)

struct {
  struct buf buf[NBUF];

  // Hash map: dev and blockno to buf
  struct buf bufmap[NBUFMAP_BUCKET];
  struct spinlock bufmap_locks[NBUFMAP_BUCKET];
} bcache;


static inline void
bbufmap_insertbucket(uint key, struct buf *b) {
  b->next = bcache.bufmap[key].next;
  bcache.bufmap[key].next = b;
}

void
binit(void)
{
  // Initialize bufmap
  for(int i=0;i<NBUFMAP_BUCKET;i++) {
    initlock(&bcache.bufmap_locks[i], "bcache_bufmap");
    // throws away the first buf in linked list, to make our lives easier
    bcache.bufmap[i].next = 0;
  }

  // Initialize buffers
  for(int i=0;i<NBUF;i++){
    struct buf *b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");
    b->valid = 0;
    b->trash = 1; // need to be evicted and re-hashed before use.
    b->lastuse = 0;
    b->refcnt = 0;
    // spread all the buffers among bufmap buckets evenly.
    bbufmap_insertbucket(i%NBUFMAP_BUCKET, b);
  }
}

// Try to look for block on device dev inside a specific bufmap bucket.
// If found, return buffer. Otherwise return null.
// Must already be holding bufmap_lock[key] for this to be thread-safe
static inline struct buf*
bbufmap_searchbucket(uint key, uint dev, uint blockno) {
  struct buf *b;
  for(b = bcache.bufmap[key].next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno && !b->trash){
      return b;
    }
  }
  return 0;
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  uint key = BUFMAP_HASH(dev, blockno);

  // printf("dev: %d, blockno: %d, locked: %d\n", dev, blockno, bcache.bufmap_locks[key].locked);
  
  acquire(&bcache.bufmap_locks[key]);

  // Is the block already cached?
  if((b = bbufmap_searchbucket(key, dev, blockno))) {
    b->refcnt++;
    release(&bcache.bufmap_locks[key]);
    acquiresleep(&b->lock);
    return b;
  }
  

  // Not cached.

  // to get a suitable block to reuse, we need to search for one in all the buckets,
  // which means acquiring their bucket locks.
  // but it's not safe to try to acquire every single bucket lock while holding one.
  // it can easily lead to circular wait, which produces deadlock.

  release(&bcache.bufmap_locks[key]);
  // we need to release our bucket lock so that iterating through all the buckets won't
  // lead to circular wait and deadlock. however, as a side effect of releasing our bucket
  // lock, other cpus might request the same blockno at the same time and the cache buf for  
  // blockno might be created multiple times in the worst case. since multiple concurrent
  // bget requests might pass the "Is the block already cached?" test and start the 
  // eviction & reuse process concurrently for the same blockno.

  // ====== eviction process ======
  // the eviction process consists of two phases:
  // 1. stealing: search for an available buf in all buckets, steal(evict) it for our new block to use.
  // 2. inserting: insert the newly evicted buf into blockno's bucket.

  // find the one least-recently-used buf among all buckets.
  // finish with it's corresponding bucket's lock held.
  struct buf *before_least = 0; 
  uint holding_bucket = -1;
  for(int i = 0; i < NBUFMAP_BUCKET; i++){
    // before acquiring, we are either holding nothing, or only locks of
    // buckets that are *on the left side* of the current bucket
    // so no circular wait can ever happen here. (safe from deadlock)
    acquire(&bcache.bufmap_locks[i]);
    int newfound = 0; // new least-recently-used buf found in this bucket
    for(b = &bcache.bufmap[i]; b->next; b = b->next) {
      if((b->trash || b->next->refcnt == 0) && (!before_least || b->next->lastuse < before_least->next->lastuse)) {
        before_least = b;
        newfound = 1;
      }
    }
    if(!newfound) {
      release(&bcache.bufmap_locks[i]);
    } else {
      if(holding_bucket != -1) release(&bcache.bufmap_locks[holding_bucket]);
      holding_bucket = i;
      // keep holding this bucket's lock....
    }
  }
  if(!before_least) {
    panic("bget: no buffers");
  }
  struct buf *newb;
  newb = before_least->next;
  
  if(holding_bucket != key) {
    // remove the buf from it's original bucket
    before_least->next = newb->next;
    release(&bcache.bufmap_locks[holding_bucket]);

    // reacquire blockno's bucket lock, for later insertion
    acquire(&bcache.bufmap_locks[key]);
  }

  // stealing phase end, inserting phase start

  // we have to check again: is this blockno now cached by another process?
  // we need to do this because during the stealing phase we don't hold
  // the bucket lock for bufmap[key] to prevent circular wait, but this can
  // lead to duplicate concurrent cache allocation for blockno. 
  if((b = bbufmap_searchbucket(key, dev, blockno))){
    b->refcnt++;

    if(holding_bucket != key) {
      // still insert newb into bufmap[key], but as a trash buffer.
      // (do not return to original bucket, to prevent deadlock)
      // trash buffers will not be accessed before being evicted and re-hashed (untrashed)
      newb->trash = 1;
      newb->lastuse = 0; // so it will be evicted and re-used earlier.

      bbufmap_insertbucket(key, newb);
    } else {
      // don't need to trash it because we havn't removed it from it's original bucket
      // and havn't done anything to alter it in any way.
    }

    release(&bcache.bufmap_locks[key]);
    acquiresleep(&b->lock);
    return b;
  }

  // still doesn't exist, now insert newb into `bcache.bufmap[key]`

  if(holding_bucket != key) {
    // should already be holding &bcache.bufmap_locks[key]
    
    // rehash and add it to the correct bucket
    bbufmap_insertbucket(key, newb);
  }

  // configure newb and return
  
  newb->trash = 0; // untrash
  newb->dev = dev;
  newb->blockno = blockno;
  newb->refcnt = 1;
  newb->valid = 0;
  release(&bcache.bufmap_locks[key]);
  acquiresleep(&newb->lock);
  return newb;
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
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint key = BUFMAP_HASH(b->dev, b->blockno);

  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->lastuse = ticks;
  }
  release(&bcache.bufmap_locks[key]);
}

void
bpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);

  acquire(&bcache.bufmap_locks[key]);
  b->refcnt++;
  release(&bcache.bufmap_locks[key]);
}

void
bunpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);

  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  release(&bcache.bufmap_locks[key]);
}

