# Lecture 28 - allocation introspection

**Source commit (PeachOS64BitCourse):** `b800188`
**SamOs commit:** L28 on `module1-64bit` branch
**Regression test:** `tests64/L28-alloc-introspection.sh`

## Why this chapter exists

`kfree(ptr)` needs to know how many blocks the allocation at
`ptr` spans. Until L28 the heap had `heap_mark_blocks_free`
that walked HAS_NEXT off the front, but no public reader for
the count. The defragmenter also needs the count - to know how
much virtual range to allocate / map / tear down.

L28 adds:

- `heap_allocation_block_count(heap, ptr)` - walks the entry
  table from the start of an allocation, returns the run
  length.
- `multiheap_get_paging_heap_for_address` - finds the sub-heap
  whose SHADOW heap covers `address`.
- `multiheap_get_heap_and_paging_heap_for_address` - given a
  pointer that might be virtual or physical, returns the
  physical sub-heap + (if virtual) the shadow sub-heap + the
  real physical address.
- `multiheap_allocation_block_count(mh, ptr)` - routes through
  the above to find the right heap for `ptr` and asks for the
  block count.
- `multiheap_allocation_byte_count(mh, ptr)` - same, multiplied
  by block size.

Nothing calls any of this at L28. The wiring to `kfree` lands
in a later lecture.

## Theory primer: counting from HAS_NEXT terminators

A multi-block allocation is encoded in the entry table as a
chain: the first block has `TAKEN | IS_FIRST | HAS_NEXT`, the
middle blocks have `TAKEN | HAS_NEXT`, the last has just
`TAKEN`. L23 fixed the bug where HAS_NEXT was dropped one block
early, so by L28 the chain is reliable.

`heap_allocation_block_count` exploits that:

```c
for (i = start; i < total; i++) {
    if (entries[i] & TAKEN) count++;
    if (!(entries[i] & HAS_NEXT)) break;
}
```

It counts TAKEN blocks until the first entry without HAS_NEXT.
Note that the `if (entries[i] & TAKEN)` guard is technically
redundant if the chain is well-formed - every block in the chain
should be TAKEN until the terminator. The guard is defensive
against a partially-corrupt table.

## Why the shadow heap is checked first

When a pointer is virtual, we want the BLOCK COUNT FROM THE
SHADOW HEAP, not the physical sub-heap. The shadow heap is
where the virtual-arena allocator's bookkeeping lives -
the physical sub-heap's bitmap reflects which blocks are
actually backing storage, not what the user asked for.

So:

```c
heap_to_check = paging_heap ? paging_heap : phys_heap;
```

If the pointer was physical to begin with, `paging_heap` is
NULL and we walk the physical sub-heap as expected.

## The upstream byte_count bug

```c
size_t multiheap_allocation_byte_count(struct multiheap* multiheap, void* ptr)
{
    return multiheap_allocation_byte_count(multiheap, ptr) * PEACHOS_HEAP_BLOCK_SIZE;
}
```

Upstream's body calls itself. Infinite recursion -> stack
overflow on first invocation. The obvious intent is to call
`multiheap_allocation_BLOCK_count` and multiply, which is what
SamOs does. Upstream probably fixes this in a later lecture
(or never; the function may never get called in the course).

## How the change lands in our tree

| File | Change |
|---|---|
| `src/memory/heap/heap.c` | new `heap_allocation_block_count`. |
| `src/memory/heap/heap.h` | prototype. |
| `src/memory/heap/multiheap.c` | new `multiheap_get_paging_heap_for_address`, `multiheap_get_heap_and_paging_heap_for_address`, `multiheap_allocation_block_count`, `multiheap_allocation_byte_count`. Also fixes the L26 semicolon typo on `multiheap_virtual_address_to_physical` (we never had it - L25 was clean - but worth noting upstream catches up here). |
| `src/memory/heap/multiheap.h` | block_count and byte_count prototypes. |

## How we verified

VGA after L28:

```
Hello 64-bit!
e820 total: 267910144
ABCMemory wasted
```

Same tokens. Helpers dormant.

## Forward look

L29 - L30 finish the multiheap (kfree wiring, defragment-by-paging
second pass). L31 implements `paging_get`. L32 calls
`multiheap_ready` from `kernel_main`.
