must pre-store everything in the hashmap.
can not put into hashmap when bget
because then you have to find out what's the least recently used page in the buf
which needs a lock, which generates contention, just like the stock xv6 design.


```c++
static struct buf* bget(uint dev, uint blockno) {
  struct buf *b;

  uint key = BUFMAP_HASH(dev, blockno);
  
  acquire(&bucket_locks[key]); // can not acquire other bucket's lock after this point (potential deadlock)

  if (block_is_already_cached_in_bucket){
	release(&bucket_locks[key]);
	return b;
  }

  // If Not cached.
  acquire(&bcache_lock);
  struct buf *earliest = 0; 
  for(int i=0;i<NBUF;i++){
	// (Here's where the problem is.)
    // find the least recently used block.
	// evict that block, use it to store our new block
  }
  if(!earliest) {
    panic("bget: no buffers");
  }
  b = earliest;
  b->xxx = yyyyy; // setup b
  release(&bcache_lock);
  release(&bucket_locks[key]);
  return b;
}
```

when cache misses, eviction of block from another bucket:
1. must acquire the other bucket's lock so it's safe to evict the block.
2. can not acquire the other bucket's lock while holding itself's lock, since it can lead to deadlock
   (cpu0 holds l[0], waiting for l[1]. cpu1 holds l[1], waiting for l[0])
3. moving bcache_lock to the beginning would work, but it's the same as stock xv6 (high lock contention)
4. releasing itself's bucket lock, search all the buckets and then reacquiring itself's bucket lock wouldn't work.
   no deadlock-free way of ensuring that between releasing and reacquiring, another cpu wouldn't have already added the
   same block to the cache.

must scan other buckets for lru buf => must hold other bucket's lock when scanning
bget cannot acquire another bucket's lock while holding self's lock (might circular wait, deadlock) => must release self's lock


takeaway: the way to prevent deadlock is to ensure **the same locking order** takes place everywhere and every time.