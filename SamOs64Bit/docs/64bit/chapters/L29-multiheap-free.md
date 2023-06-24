# Lecture 29 - multiheap_free + paging_get_physical_address

**Source commit (PeachOS64BitCourse):** `80c0067`
**SamOs commit:** L29 on `module1-64bit` branch
**Regression test:** `tests64/L29-multiheap-free.sh`

## Why this chapter exists

L25 - L28 built the routing primitives. L29 finally writes the
per-allocation free.

The interesting case is freeing a virtual-arena pointer. The
allocation maps several scattered physical blocks behind a
contiguous virtual range; freeing it means:

1. For every block in the virtual range, look up its physical
   backing via the page tables.
2. Free each physical backing in its owning physical sub-heap.
3. Free the virtual range itself on the shadow heap (which
   fires the per-block free callback that tears down the PT
   entries).

The physical case is straightforward: route to the owning
sub-heap and call `heap_free`.

## Theory primer: bounded recursion

```c
void multiheap_free(struct multiheap* mh, void* ptr) {
    ...
    if (paging_heap) {
        for (each block in the allocation) {
            void* phys = paging_get_physical_address(..., vaddr_for_block);
            multiheap_free(mh, phys);   // <-- recursion
        }
        heap_free(paging_heap->paging_heap, ptr);
    } else {
        heap_free(phys_heap->heap, ptr);
    }
}
```

The recursion is bounded by one level. The inner
`multiheap_free(mh, phys)` is called with a PHYSICAL pointer.
`multiheap_is_address_virtual(mh, phys)` returns false (phys is
below `max_end_data_addr`), so the inner call takes the else
branch and returns without further recursion.

The cost is O(N blocks). For a typical kmalloc(4096) that's
N=1.

## paging_get is stubbed at L29

The virtual-arena branch needs `paging_get_physical_address`,
which itself needs `paging_get`. Upstream L29 calls `paging_get`
unconditionally even though the real implementation lands at
L31 - relying on undefined-reference warnings being non-fatal.
Our build runs `-Wall -Werror` and would refuse, so SamOs adds
a NULL-returning stub for `paging_get` at L29.

`multiheap_free` is dormant at L29 anyway (`kfree` is still a
no-op in kheap.c), so the stub never actually fires. When L31
ports, the stub gets replaced with the real walk and the
virtual-arena branch goes live.

## The rename: multiheap_free -> multiheap_free_heap

Upstream takes the well-known short name `multiheap_free` for
the per-allocation free, matching libc semantics. The old
"tear down everything" function is renamed to
`multiheap_free_heap`. No caller exists for either in the SamOs
tree at L29, so the rename is mechanical.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/memory/heap/heap.c` | `heap_address_to_block` un-staticed. Forward-decl removed (was static). |
| `src/memory/heap/heap.h` | prototype. |
| `src/memory/paging/paging.c` | NEW `paging_get` STUB returning NULL. NEW `paging_get_physical_address` using `paging_get` + entry.address<<12 + offset. |
| `src/memory/paging/paging.h` | prototypes. |
| `src/memory/heap/multiheap.c` | old `multiheap_free` renamed to `multiheap_free_heap`. NEW `multiheap_free(mh, ptr)`. |
| `src/memory/heap/multiheap.h` | both prototypes. |

## How we verified

VGA after L29:

```
Hello 64-bit!
e820 total: 267910144
ABCMemory wasted
```

Same tokens. `multiheap_free` is wired but no caller invokes it.

## Forward look

L30 finishes the multiheap (probably the alloc-side callback so
the shadow heap's allocation actually maps a physical block).
L31 implements `paging_get` for real. L32 calls
`multiheap_ready` from `kernel_main`.
