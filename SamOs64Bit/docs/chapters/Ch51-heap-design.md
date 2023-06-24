# Ch 47 - Our Kernel Heap Design

**Book pages:** 146-149 (Part 5)
**Code in this chapter:** none, prose

## The two parts of the heap

```
struct heap {
    struct heap_table* table;   // bookkeeping
    void* saddr;                // start address of the data pool
};

struct heap_table {
    HEAP_BLOCK_TABLE_ENTRY* entries;   // one byte per data block
    size_t total;                       // number of blocks tracked
};
```

The heap is described by a `struct heap` that holds:

1. **A data pool** at `saddr`. This is where allocations actually live. We pick the start address up front and keep it fixed. The pool must start on a 4 KiB boundary.
2. **A table** of bookkeeping bytes. Each entry corresponds to one 4 KiB block in the pool. If the pool starts at 0x200000 and has N blocks, entry 0 describes 0x200000-0x200FFF, entry 1 describes 0x201000-0x201FFF, and so on.

The table lives separately from the pool. We pick its address up front too.

## Table entry flags

Each entry is a single byte with three flag bits:

```
bit 0 : TABLE_ENTRY_TAKEN   (1 = used, 0 = free)
bit 6 : IS_FIRST            (1 = first block of an allocation)
bit 7 : HAS_NEXT            (1 = the next block belongs to the same allocation)
```

(Bits 1-5 are unused right now.)

So a 12 KiB allocation occupying blocks 0, 1, 2 of the table would have:

| Index | TAKEN | IS_FIRST | HAS_NEXT | Memory |
|-------|-------|----------|----------|--------|
| 0 | 1 | 1 | 1 | 0x200000 - 0x200FFF |
| 1 | 1 | 0 | 1 | 0x201000 - 0x201FFF |
| 2 | 1 | 0 | 0 | 0x202000 - 0x202FFF |
| 3 | 0 | 0 | 0 | (free) |

## Why block-based

The book's pitch:

- Every allocation is rounded up to a multiple of 4 KiB.
- Asking for 100 bytes still consumes one full block (waste).
- Asking for 4097 bytes consumes two blocks (1 byte over the boundary, one whole second block).

In return:

- No fragmentation within a block: we never split.
- Allocation is a linear scan over the table looking for N consecutive free entries.
- Free is a walk of the HAS_NEXT chain marking blocks free.
- Block addresses are page-aligned by construction. Once we turn paging on, each allocation maps to whole pages without splitting.

## How allocation works

`kmalloc(size)`:

1. Round size up to a multiple of `HEAP_BLOCK_SIZE` (4096).
2. Compute N = blocks needed.
3. Scan the table for N consecutive free entries.
4. Mark them: first one gets TAKEN + IS_FIRST + HAS_NEXT (if N > 1); middle ones get TAKEN + HAS_NEXT; last one gets just TAKEN.
5. Return `saddr + (first_index * BLOCK_SIZE)`.

If no run is found, return NULL.

## How free works

`kfree(ptr)`:

1. Compute the table index: `(ptr - saddr) / BLOCK_SIZE`.
2. Walk forward: clear TAKEN/IS_FIRST/HAS_NEXT on each entry whose HAS_NEXT was set on the previous one.
3. Stop at the first entry without HAS_NEXT (that's the last block of the allocation; clear it too).

The caller has to pass the exact address `kmalloc` returned. Passing an interior pointer or the wrong allocation breaks the linked walk.

## What this design does not handle

- Concurrent allocators. Single-threaded kernel only.
- Realloc. Caller frees and re-allocates.
- Best fit. We take the first run we find.
- Sub-block fragmentation. There is no such thing here because blocks are not split.
- Pool exhaustion. We just return NULL and the caller decides what to do.

The implementation in the next chapter mirrors this design 1:1.
