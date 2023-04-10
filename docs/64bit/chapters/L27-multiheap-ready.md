# Lecture 27 - multiheap_ready + virtual-arena shadow heaps

**Source commit (PeachOS64BitCourse):** `c8a4fef`
**SamOs commit:** L27 on `module1-64bit` branch
**Regression test:** `tests64/L27-multiheap-ready-and-shadow.sh`

## Why this chapter exists

L25 set up the address-routing primitives. L26 added per-heap
callbacks. L27 stitches them together into the second-pass
defragmenter's data structures: a SHADOW heap per
DEFRAGMENT_WITH_PAGING sub-heap, living in the virtual arena
above `max_end_data_addr`.

`multiheap_ready` is the one-shot setup. At L27 it's defined but
nothing calls it. Upstream wires the call at "Lecture 32 -
calling multiheap_ready". SamOs follows.

## Theory primer: the shadow heap layout

For each physical sub-heap covering `[saddr, eaddr)`:

```
physical (sub-heap):
  saddr ........... eaddr

virtual arena (shadow):
  max_end + saddr ........... max_end + eaddr
```

A simple offset by `max_end_data_addr`. The shadow heap has its
own bitmap (allocated from the multiheap's starting heap), so
free/alloc state on the shadow is independent of the underlying
physical sub-heap. The point is to expose CONTIGUOUS virtual
ranges even when the underlying physical free blocks are scattered.

At setup time we install the virtual range into the live PML4
with flags=0 (not-present). First access to a shadow page would
fault. The alloc callback (later lecture) will turn the page on
and point it at a free physical block from one of the sub-heaps.
The free callback (installed now) unmaps the page.

## Why we need `paging_current_descriptor`

`multiheap_paging_heap_free_block` is a callback fired from
inside `heap_mark_blocks_free` - which has no access to the
multiheap or the kernel paging descriptor. It needs the live
PML4 by some other route.

Threading a desc through every callback would be invasive. A
global getter is the path of least resistance:

```c
struct paging_desc* paging_current_descriptor(void) {
    return current_paging_desc;
}
```

`current_paging_desc` is set in `paging_switch`, so it's the
correct live descriptor for whatever address space we're in.

## The unmap idiom

```c
paging_map(paging_current_descriptor(), ptr, NULL, 0);
```

`paging_map` writes the PT entry: `address = phys >> 12,
present = flags & PRESENT, read_write = flags & WRITEABLE`. With
`phys = NULL` and `flags = 0`, the entry becomes "address=0,
present=0, rw=0" - exactly the "not mapped" state.

(`paging_invalidate_tlb_entry` is fired internally by
`paging_map` when the slot was non-null on entry, so the TLB
gets flushed automatically.)

## Upstream typos worked around

| Upstream code | Issue | SamOs fix |
|---|---|---|
| `heap_kalloc(...)` | function does not exist | use `heap_zalloc` (same effect: zeroed struct heap) |
| `out:` label after the unconditional `return res;` flow | dangling | omitted |

The diff comment uses `heap_kalloc` - probably intended as
`heap_zalloc`. Upstream eventually fixes this. We just call
`heap_zalloc` directly.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/memory/paging/paging.c` | new `paging_current_descriptor()`. |
| `src/memory/paging/paging.h` | prototype. |
| `src/memory/heap/multiheap.h` | `struct multiheap_single_heap` gains `paging_heap`. `struct multiheap` gains `flags`. New `MULTIHEAP_FLAG_IS_READY`. Prototype `multiheap_ready`. |
| `src/memory/heap/multiheap.c` | new `multiheap_paging_heap_free_block` (the unmap free callback). New `multiheap_ready` (per-sub-heap shadow setup). |

## How we verified

VGA after L27:

```
Hello 64-bit!
e820 total: 267910144
ABCMemory wasted
```

Same tokens as L23..L26. `multiheap_ready` is dormant.

## Forward look

L28 - L30 fill in the second-pass allocator that actually uses
the shadow heaps. L32 calls `multiheap_ready` from `kernel_main`
so the arena gets stood up at boot. L31 implements paging_get.
