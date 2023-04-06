# Lecture 23 - multi-heap part 3

**Source commit (PeachOS64BitCourse):** `32da59f`
**SamOs commit:** L23 on `module1-64bit` branch
**Regression test:** `tests64/L23-paging-switch-and-drain.sh`

## Why this chapter exists

Three threads converge in L23:

1. The heap code is widened to 64-bit (int -> int64_t for block
   indices). The 4-KiB block size means a 32-bit signed index
   tops out around 8 TiB; OK today but not a hill to die on.
2. Two latent bugs in the heap's free-list bookkeeping get fixed
   (see below).
3. The kernel finally calls `paging_switch` on the freshly-built
   `kernel_paging_desc` and runs to exhaustion against the
   multiheap to prove the whole stack is alive.

## Theory primer

### Bug 1: heap_get_start_block returned a partial run

Before:

```c
if (bs == -1) return -ENOMEM;
return bs;
```

`bs` is set the moment the first FREE block is seen in a run. It
stays set across the rest of the scan unless interrupted by a
non-free block (which resets `bs = -1`). But if the LAST run in
the table is FREE but only `bc < total_blocks` long, we exit the
loop with `bs != -1` and `bc < total_blocks` - and the old code
returns `bs` anyway. `heap_mark_blocks_taken` then walks past
the table end, corrupting whatever comes after.

After:

```c
if (bc != total_blocks) return -ENOMEM;
return bs;
```

The condition that actually expresses success is "I found
`total_blocks` consecutive free blocks", not "I saw at least one
free block".

### Bug 2: heap_mark_blocks_taken dropped HAS_NEXT one block early

Before:

```c
for (i = start; i <= end; i++) {
    heap->table->entries[i] = entry;
    entry = TAKEN;
    if (i != end - 1) entry |= HAS_NEXT;   // <-- BUG
}
```

The loop iterates `start..end`. After the assignment, `entry` is
reset to TAKEN for the NEXT iteration. The `if (i != end - 1)`
guard adds HAS_NEXT to the next-iteration entry. But on the
iteration where `i == end - 1`, we're writing to slot `end - 1`
this turn and slot `end` next turn. So we WANT slot `end - 1`
to keep HAS_NEXT (because slot `end` is still part of the
allocation), and the LAST slot we should drop HAS_NEXT for is
slot `end` itself.

`if (i != end - 1)` drops HAS_NEXT for the entry that writes to
slot `end - 1` -> slot `end - 1` gets TAKEN without HAS_NEXT.
Then `heap_free` walks until the first entry without HAS_NEXT,
stops at slot `end - 1`, and never frees slot `end`. Slow leak.

After: `if (i != end)` - only the very last slot drops
HAS_NEXT.

### Why minimal heap now spans the whole E820 region

L20 capped the minimal heap at `SAMOS_HEAP_SIZE_BYTES` (100 MiB).
That number was carried over from the pre-multiheap era. With
multiheap there's no need to leave room - additional E820
regions become additional sub-heaps independently. So L23
extends the minimal heap to the full length of the chosen E820
entry. Side effect: the cap was previously hiding the bugs in
`heap_get_start_block` and `heap_mark_blocks_taken` because the
heap never got drained close to its limit; once we drain to
exhaustion in `kernel_main`, the bugs trigger immediately. L23
fixes them in the same commit.

### Sub-heap clamp above MINIMAL_HEAP_ADDRESS

E820 enumerates physical RAM. Multiple entries can sit BELOW the
minimal heap data pool start (0x01100000) - the low-memory
chunk (0..640 KiB usable) is the obvious one. We don't want to
register a sub-heap that overlaps the minimal heap. So:

```c
if (base_addr < MINIMAL_HEAP_ADDRESS) base_addr = MINIMAL_HEAP_ADDRESS;
if (end_addr <= base_addr) continue;
```

Clamp UP; drop if nothing's left.

### Force-mapping 0..1 MiB

`paging_map_e820_memory_regions` previously only mapped E820
type-1 regions. The first 1 MiB is mostly reserved (BIOS, VGA,
EBDA), so under the L21 behaviour we'd paging_switch and
immediately fault writing to VGA at 0xB8000.

L23 prepends an unconditional `paging_map_to(desc, 0, 0,
0x100000)`. Yes, that maps reserved BIOS regions too. The
alternative - faulting on every legitimate VGA write - is
worse. Once we have a real fault handler we can revisit.

### kmalloc no longer panics

L20's `kmalloc` panicked on NULL because the early callers
assumed infallibility. L23 needs callers to be able to detect
OOM (the drain loop relies on `kmalloc` returning NULL when
exhausted), so the panic moves out. Callers that NEED non-NULL
check explicitly.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/memory/heap/heap.c` | block indexes widened to int64_t throughout (including forward decls). Bug fixes in `heap_get_start_block` and `heap_mark_blocks_taken`. Comments document both. |
| `src/memory/heap/kheap.c` | minimal heap extended to the whole chosen E820 region. Sub-heaps clamped above `SAMOS_MINIMAL_HEAP_ADDRESS`, dropped if empty after clamp. `kmalloc` no longer panics on NULL. |
| `src/memory/paging/paging.c` | `paging_map_e820_memory_regions` prepends an unconditional map of 0..0x100000. |
| `src/kernel.c` | panic on `paging_desc_new` NULL. `paging_switch(kernel_paging_desc)`. Drain loop. Print "Memory wasted". Drain uses 1 MiB chunks (SamOs deviation - upstream uses 4 KiB which makes the test take tens of seconds in TCG). |

## How we verified

VGA after L23:

```
Hello 64-bit!
e820 total: 267910144
ABCMemory wasted
```

"Memory wasted" proves three things in one:

1. `paging_switch` to the new PML4 did not fault - the print
   would have died on the VGA write otherwise.
2. The multiheap actually exhausts. `heap_get_start_block`
   returns -ENOMEM correctly once no run of N blocks exists.
3. `kmalloc` returning NULL is observable downstream. No silent
   panic-on-NULL hides the OOM.

L21 + L22 regression tokens are gone by design (L21's "paging
map built" was removed in the L23 rewrite). The L21/L22 tests
are obsolete from this commit onward; they remain in tests64/
as historical record.

## Forward look

L24-L30 finish the multiheap (free-by-range routing, callback
handlers, type queries, the paging-defragment second pass). L31
implements `paging_get`. After that the IO/IDT/keyboard/process
rebuild begins.
