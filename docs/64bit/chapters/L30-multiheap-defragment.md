# Lecture 30 - multi-heap part 9: defragment-by-paging

**Source commit (PeachOS64BitCourse):** `da08806`
**SamOs commit:** L30 on `module1-64bit` branch
**Regression test:** `tests64/L30-multiheap-defragment.sh`

## Why this chapter exists

L30 is the payoff for L20 - L29. The second-pass allocator gets
its real implementation. When the first-pass walk fails to find
a contiguous physical run in any sub-heap, the second pass takes
N scattered physical blocks and exposes them as one contiguous
virtual range through the page tables.

Three pieces have to land in the same commit for this to work:

1. `struct heap` gains running counters
   (`total_blocks`, `free_blocks`, `used_blocks`) so the second
   pass can ask "does this physical sub-heap have N free blocks
   I could pull?" in O(1).
2. `multiheap_is_ready` / `multiheap_can_add_heap` predicates,
   and the late-add gate. Once the multiheap is ready (shadow
   heaps built, virtual arena reserved), the sub-heap set is
   fixed - any new add would lack a shadow.
3. `multiheap_alloc_paging` and the real
   `multiheap_alloc_second_pass`.

## Theory primer: how a virtual allocation forms

```
ask = kpalloc(16384)  -> N=4 blocks needed

1) first-pass: walk sub-heaps, fail because no single sub-heap
   has 4 contiguous free blocks.

2) second-pass: round size up; call multiheap_alloc_paging which
   walks DEFRAGMENT_WITH_PAGING sub-heaps looking for one whose
   PHYSICAL free_blocks >= 4 (regardless of contiguity), then
   allocates a 4-block CONTIGUOUS slot in its SHADOW heap.

3) loop 4 times:
     phys = heap_zalloc(chosen->heap, 4096)   // any free block
     paging_map(desc, vcur, phys, RW|P)        // alias virtual
     vcur += 4096
```

The 4 physical blocks need not be contiguous; the virtual range
IS contiguous because we install consecutive PT entries.

## Why eaddr-range routing has to be inclusive

(Already established in L25.) For an allocation that lands at
the very top of the virtual arena, `multiheap_is_address_virtual`
needs to return true for `ptr == max_end_data_addr`. The L25
`<=` boundary fits.

## Why the ready gate

`multiheap_ready` snapshots the sub-heap list to build shadow
heaps and reserves their virtual ranges in the PML4. Adding a
sub-heap AFTER ready would mean:

- No shadow heap for the new sub-heap (so it can't back virtual
  allocations).
- No virtual-arena range reserved (so even if you fixed that,
  `multiheap_is_address_virtual` ordering breaks).

The cheap defense is to refuse the add. Caller gets `-EINVARG`
and (in our kheap) hits the panic-on-error path. Better a noisy
boot than silent corruption.

## The heap counter ordering subtlety

`heap_mark_blocks_free` now fires the callback BEFORE the
HAS_NEXT terminator check, where L26 fired it AFTER. The new
ordering means the LAST block in the chain ALSO gets the
callback - critical for the virtual-arena teardown, which
needs the page-unmap callback to fire for every block in the
allocation including the terminator.

`heap_malloc_blocks` increments `used_blocks` after the mark
operation; `heap_mark_blocks_free` increments `total_blocks_freed`
inside the loop and bulk-applies at the end. Symmetric.

## Upstream bug noted

Upstream's panic message has a typo: `"this mus ta bug"`. SamOs's
copy reads `"multiheap second pass: paging heap promised more
blocks than physical has"` - clearer if it ever fires.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/memory/heap/heap.h` | adds `total_blocks`, `free_blocks`, `used_blocks` to `struct heap`. |
| `src/memory/heap/heap.c` | `heap_create` initialises the counters. `heap_malloc_blocks` post-bumps used / pre-decrements free. `heap_mark_blocks_free` reorders callback-vs-terminator check, accumulates a freed count, bulk-decrements used. |
| `src/memory/heap/multiheap.c` | new `multiheap_is_ready`, `multiheap_can_add_heap`. `multiheap_add_heap` refuses if `!can_add_heap`. New `multiheap_alloc_paging` (find a paging sub-heap with capacity, get a shadow range). Real `multiheap_alloc_second_pass` (loop heap_zalloc + paging_map per block). |
| `src/memory/heap/multiheap.h` | new predicates + the queue helpers that L25 had only in .c. |

## How we verified

VGA after L30:

```
Hello 64-bit!
e820 total: 267910144
ABCMemory wasted
```

Same tokens. The defragment path is only reached when
`multiheap_palloc` falls through after a failed first pass. Our
kernel_main uses `kmalloc` (first-pass-only), so the new code
is dormant.

## Forward look

L31 implements `paging_get` (replaces the L29 NULL stub) so the
virtual-arena free path actually works. L32 calls
`multiheap_ready` from kernel_main and the whole stack goes
live. Post that, the IO/IDT/keyboard/process rebuild begins.
