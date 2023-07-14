# Lecture 80 - krealloc

The L78 vector commit slipped in a stub `krealloc` (kzalloc + memcpy
+ kfree) so `vector_push_back` would link. L80 replaces it with a
real implementation that walks down through `multiheap_realloc` to
`heap_realloc` and handles three cases without copying when it can
avoid it.

## The chain

```
krealloc(p, n)
   -> multiheap_realloc(kernel_multiheap, p, n)
        -> heap_realloc(owning_heap, p, n)
             -> shrink | grow-in-place | grow-with-move
```

`krealloc` is a one-liner forward. `multiheap_realloc` reuses
`multiheap_get_heap_and_paging_heap_for_address` to find the owning
sub-heap. The virtual-arena (paging-defragment) case panics: a real
implementation would need to remap the virtual range and keep the
underlying physical blocks in sync, and we have not done that work
yet. `kmalloc`/`kpalloc` callers in the kernel today land in the
physical arena, so the panic is unreachable in practice.

`heap_realloc` is where the work is. It walks the block bitmap from
the old allocation's start to find its end (a contiguous TAKEN run
with HAS_NEXT chaining), then picks one of three paths:

### Shrink

`new_blocks < old_blocks`: shorten the run in place. The block at
`old_blocks - new_blocks_count` (the new last block) gets its
HAS_NEXT bit cleared. The trailing blocks are then handed back to
the free pool via `heap_mark_blocks_free` starting from the next
block. No memcpy is needed because the surviving prefix already
holds the caller's data.

### Grow in place

`new_blocks > old_blocks` AND the blocks immediately after the
current run are free. We test that with the new
`heap_is_block_range_free` helper, which scans the bitmap inclusive
range `[start, end]` and returns false the moment it sees a TAKEN
bit. If the trailing range is clean, we extend the run: the old
last block gets HAS_NEXT set, each new block is marked TAKEN +
HAS_NEXT, the final block has HAS_NEXT cleared. Again no copy.

### Grow with move

`new_blocks > old_blocks` AND the trailing range is not free. Fall
back to the obvious `heap_zalloc(new_size)` + `memcpy(old_size)` +
`heap_free(old_ptr)`. This is the case the L78 stub always took, so
that path is well exercised; the wins from L80 are the two
no-copy paths.

## Block accounting

`heap_realloc` keeps `used_blocks` and `free_blocks` consistent
without the running tallies running away:

- shrink: `heap_mark_blocks_free` already adjusts both counters.
- grow in place: a small manual `used_blocks += extra` /
  `free_blocks -= extra`, because we did not go through
  `heap_malloc_blocks`.
- grow with move: zalloc + free both go through the existing paths.

## Why the helper is exclusive of the start block

`heap_is_block_range_free(heap, start, end)` is documented as an
inclusive walk. The caller in `heap_realloc` passes
`start = old_last_block + 1` (the first NEW block we want to claim)
and `end = old_last_block + extra` (the final new block). That is
correct: we are asking "is the strictly-after region free", not "is
the current allocation still ours". Doing it the other way would
always return false because the current allocation's blocks are by
definition TAKEN.

## What L80 unlocks

`vector_push_back` (and any future container that grows in place)
now gets the upstream behaviour: the common case (free space behind
the buffer) is a bitmap diff, not a copy. Heap fragmentation is the
only thing that forces the copy path.

The L78 stub is gone; `kheap.c` is back to one short function per
public entry point.
