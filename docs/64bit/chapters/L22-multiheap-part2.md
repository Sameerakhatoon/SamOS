# Lecture 22 - multi-heap part 2

**Source commit (PeachOS64BitCourse):** `4d12f09`
**SamOs commit:** L22 on `module1-64bit` branch
**Regression test:** `tests64/L22-kzalloc-unstubbed.sh`

## Why this chapter exists

L20 stood up the multi-heap with a deliberately-incomplete API
(kzalloc / kfree stubbed, no paging-aware allocator). L22 patches
in two things:

1. Every heap region added to the multiheap gets explicitly
   page-aligned at both ends. This is necessary preparation for
   the future paging-defragment second pass, which has to
   manipulate whole pages.
2. `kzalloc` unstubbed. A new pair `kpalloc` / `kpzalloc` adds
   the paging-defragment entry points (their second pass is
   still a NULL stub but the front door is wired).

The first visible effect: L21's deferred-bug marker "L21 paging
desc NULL (kzalloc stub)" flips to "L21 paging map built" because
`paging_desc_new`'s internal kzalloc now returns memory.

## Theory primer

### Why page-align in kheap_init

The minimal heap's data pool starts at
`SAMOS_MINIMAL_HEAP_ADDRESS = 0x01100000` (17 MiB). 0x01100000 is
already 4 KiB-aligned, so the L22 round-up in practice is a
no-op for the minimal heap data pool. But the END of the pool is
`SAMOS_MINIMAL_HEAP_ADDRESS + SAMOS_HEAP_SIZE_BYTES = 0x01100000
+ 0x06400000 = 0x07500000` - also 4 KiB aligned.

For the additional E820 sub-heaps the round is more interesting:
QEMU's "low chunk" entry can start above 1 MiB but the high one
typically starts at 0x100000 (aligned) and extends to wherever.
BIOSes occasionally give bases like 0xC0000 + a few bytes for
chipset-reserved regions. The round-up / round-down pair makes
the multiheap robust against that.

The alignment direction is the same as L21's E820 paging map:
base UP (skip any sub-page leftover at the start), end DOWN
(skip any leftover at the end). Symmetric.

### kpalloc / kpzalloc - why a separate front door

`kmalloc` -> `multiheap_alloc` does first-pass only. If every
sub-heap is fragmented enough that no contiguous run of N blocks
exists, allocation fails - even if the TOTAL free space is huge.

`kpalloc` -> `multiheap_palloc` does first-pass, then second-pass
defragment-by-paging. The idea (lands in a later lecture): when
N contiguous physical blocks are unavailable, remap N free
PHYSICAL pages from different sub-heaps to one contiguous
VIRTUAL range. That trades a TLB shootdown for a successful
alloc.

The split exists because most callers do not want paging
defragment: it has side effects (page-table modifications, TLB
flushes) that some callers cannot tolerate (e.g. the paging code
itself, which would recurse). The default is the conservative
`kmalloc`; callers that explicitly know defragment is fine ask
for `kpalloc`.

`kpzalloc` is just `kpalloc` + `memset 0`. Same shape as
kzalloc.

### kfree still stubbed

`multiheap_free` (the API that figures out which sub-heap a
pointer belongs to via `heap->eaddr` range check and routes
heap_free into the right sub-heap) is not in yet. So `kfree`
remains a no-op until a later lecture wires it. Allocs leak
right now - acceptable in a kernel where everything we allocate
lives forever.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/memory/heap/kheap.c` | `kheap_init`: page-align both ends of the minimal-heap data pool. Same alignment treatment for every E820 sub-heap. Include `paging.h`. `kzalloc` unstubbed. New `kpalloc` and `kpzalloc` routed through `multiheap_palloc`. |
| `src/memory/heap/kheap.h` | adds `kpalloc` and `kpzalloc` prototypes. |

## How we verified

VGA after L22:

```
Hello 64-bit!
e820 total: 267910144
ABCL21 paging map built
```

The L21 marker is the success string now. The test asserts both
that string and the absence of "L21 paging desc NULL" - so a
future accidental re-stub of kzalloc would fail the test
immediately.

## Forward look

L23..L30 builds the rest of the multi-heap: per-heap routing for
free, the second-pass paging defragmenter, callback handlers for
custom heap types, and the multiheap_get API for queries. L31
implements `paging_get` and other paging utilities.

`kfree` remains stubbed across this whole arc; it gets a real
implementation when `multiheap_free_by_ptr` (range-check-based
routing) lands.
