## locking
operation to the `pageref` array should be protected by locks. otherwise can lead to memory leak or double-freeing:

example of memory leak:
```
parent: allocates page p (pageref[p] = 1)
parent: forks (pageref[p] = 2)
parent: tries to write to p, triggering an interrupt
parent: if pageref[p] > 1 then start copying page (pageref[p] = 2)
--- scheduler switch to child
child : tries to free page p (decreases reference by one), ditches reference to p (pageref[p] = 1)
--- scheduler switch to parent
parent: allocate new page q, copy p to q, map new page q as writable, ditches reference to p
```
after this, neither the child nor the parent has a reference to p.
the physical memory occupied by p can never be freed.

usertests will fail with:
```
FAILED -- lost some free pages 32442 (out of 32448)
```