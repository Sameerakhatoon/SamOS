# Lecture 26 - per-heap callback handlers

**Source commit (PeachOS64BitCourse):** `053878e`
**SamOs commit:** L26 on `module1-64bit` branch
**Regression test:** `tests64/L26-heap-callbacks.sh`

## Why this chapter exists

The defragmenter that lands in a later lecture needs to do work
per block that gets allocated or freed inside a
DEFRAGMENT_WITH_PAGING heap - specifically, set up or tear down
the virtual-arena page-table aliases. The cleanest way to slot
that into the existing heap walks is a function pointer on
`struct heap` that the walk loop dispatches to.

L26 wires the function-pointer fields, the setter, and the call
sites. No heap actually registers callbacks at L26.

## API

```c
typedef void* (*HEAP_BLOCK_ALLOCATED_CALLBACK_FUNCTION)(void* ptr, size_t size);
typedef void  (*HEAP_BLOCK_FREE_CALLBACK_FUNCTION)(void* ptr);

void heap_callbacks_set(struct heap* heap,
                        HEAP_BLOCK_ALLOCATED_CALLBACK_FUNCTION alloc_cb,
                        HEAP_BLOCK_FREE_CALLBACK_FUNCTION free_cb);
```

Either callback may be NULL. NULL is the default (struct heap is
zero-initialised by heap_create's memset).

## Theory primer: per-block vs per-allocation

The alloc callback fires INSIDE heap_mark_blocks_taken's loop -
once per block in a multi-block allocation. So a kmalloc(16384)
on a 4 KiB heap fires the callback four times. The "size" arg
is always SAMOS_HEAP_BLOCK_SIZE.

Why per-block? Because the defragment-by-paging arena maps each
4 KiB block to its own virtual page. The page-table update has
to happen per page, not per allocation. Going through one
callback per block lines up the heap walk and the page-table
walk neatly.

The free callback fires INSIDE heap_mark_blocks_free's loop -
also per block - and AFTER the entry has been marked
HEAP_BLOCK_TABLE_ENTRY_FREE. A callback that wants to inspect
the post-free state (to assert invariants in debug builds, for
instance) sees the truth.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/memory/heap/heap.h` | new callback typedefs. Two function-pointer fields on `struct heap`. `heap_callbacks_set` prototype. |
| `src/memory/heap/heap.c` | `heap_callbacks_set` impl. Per-block call site in `heap_mark_blocks_taken`. Per-block call site in `heap_mark_blocks_free`. |

## How we verified

VGA after L26:

```
Hello 64-bit!
e820 total: 267910144
ABCMemory wasted
```

Same tokens. No heap registers a callback, so both pointer fields
are NULL and neither call site does anything. The test catches
two failure modes:

1. A null-pointer crash from a missing NULL check on the
   function-pointer fields would have killed us between the
   first kmalloc and "Memory wasted".
2. An off-by-one mis-placement of the callback inside the loop
   (e.g. dereferencing entries that were just overwritten with
   FREE) would also crash.

Neither happened.

## Upstream typo

The upstream L26 commit dropped a semicolon on
`multiheap_virtual_address_to_physical`:

```c
void* phys_addr = ...;
return phys_addr      // <-- missing ;
}
```

That makes the file unbuildable in isolation. Upstream fixes it
in a later lecture. SamOs does NOT replicate the typo - L25
already returns with a semicolon and L26 does not touch that
function in our tree.

## Forward look

L27 - L30 wire callbacks for the defragment heap, implement
multiheap_zalloc / kfree / multiheap_free, and stand up the
second-pass paging-defragmenter. L31 implements paging_get.
