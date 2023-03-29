# Lecture 16 - dynamic kernel heap size + accounting

**Source commit (PeachOS64BitCourse):** `0b98014`
**SamOs commit:** L16 on `module1-64bit` branch
**Regression test:** `tests64/L16-dynamic-kheap.sh`

## Why this chapter exists

L10's `kheap_init()` hard-coded the heap size to
`SAMOS_HEAP_SIZE_BYTES`. That's fine for one heap, but the
upcoming multi-heap work (L20+) needs to carve the physical
memory into several regions of differing sizes - one per E820
"usable" range, possibly. L16 is the small first step:

1. `kheap_init` takes a `size` parameter.
2. Add `kheap_get()` so callers can fetch the live heap.
3. Add `heap_total_size`, `heap_total_used`,
   `heap_total_available` for bookkeeping + later
   multi-heap routing.
4. `kmalloc` and `kheap_init` failure paths upgrade from
   "silently print" to `panic`.
5. Add `itoa` so we can actually print sizes.
6. Un-static `paging_is_aligned` and declare it in `paging.h` so
   future callers (process-creation code in L40+) can validate
   alignment before mapping.

## Theory primer

### Why bump kheap_init failure to panic

L10's `kheap_init` ended with:

```c
if (res < 0) print("Failed to create heap\n");
```

then returned. The very next caller would do `kmalloc(50)`. If
the heap actually failed to create, `heap->table` is the
uninitialised global static (zeros) and `heap_malloc` walks a
zero-length table - returns 0. The caller derefs the 0. The
fault is now several lines downstream of the real bug.

L16 hardens this: a failed `heap_create` is unrecoverable - we
have no kheap, so we cannot do anything else useful. Panic with
the actual cause printed.

Same reasoning for `kmalloc`'s NULL handling. Early kernel code
treats malloc as infallible (most callers do not check); making
that assumption explicit is safer than letting a stale NULL
cause a `#PF` later when paging has been switched and the source
location is hard to recover.

### itoa subtlety: convert positive to negative

```c
if (i >= 0) { neg = 0; i = -i; }
while (i) {
    text[--loc] = '0' - (i % 10);
    i /= 10;
}
```

Two things going on:

1. **Loop variable is always non-positive.** We negate
   non-negative inputs and leave negative inputs alone, then
   loop "upward" toward zero. This avoids the INT_MIN overflow
   bug: `-INT_MIN` is undefined for two's complement, but
   `INT_MIN` itself is representable, and we never try to flip
   it.
2. **Digit extraction uses `'0' - (i % 10)`.** Because i is
   non-positive, `i%10` is non-positive too (well-defined in
   C99/C11). For i=-15, `i%10 = -5`, and `'0' - (-5) = '5'`.

The result is a pointer into a `static char text[12]` - explicitly
not reentrant. Documented in the header comment.

### Why heap_total_used iterates linearly

```c
for (size_t i = 0; i < table->total; i++)
    if (heap_get_entry_type(...) == TAKEN) total += BLOCK_SIZE;
```

The heap's free-list-equivalent is the per-block entry table. There
is no separate "used bytes" counter. Linear scan over the entry
table is O(table->total). For a 100 MiB heap with 4 KiB blocks
that's 25600 entries - microseconds. Acceptable for a status
print. If multi-heap ever needs this in a hot path, the
bookkeeping can become an incremental counter; L16 stays simple.

### `paging_is_aligned` un-staticed

Previously a file-private helper of `paging.c`. The future
process-creation path (L40+) needs to validate that user-supplied
addresses are page-aligned before calling `paging_map`, so the
function moves to public API. No behavior change - just a
visibility one.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/memory/heap/kheap.h` | `kheap_init(size_t)`, new `kheap_get(void)`. Forward-declare `struct heap` so we don't have to include `heap.h` here. |
| `src/memory/heap/kheap.c` | `kheap_init` takes size. Failure paths panic. `kheap_get` added. `kmalloc` panics on null. |
| `src/memory/heap/heap.h` | adds `heap_total_size`, `heap_total_used`, `heap_total_available`. |
| `src/memory/heap/heap.c` | implements the three. |
| `src/string/string.h` + `.c` | adds `itoa`. |
| `src/memory/paging/paging.c` | drops `static` from `paging_is_aligned`. |
| `src/memory/paging/paging.h` | adds prototype for `paging_is_aligned`. |
| `src/kernel.c` | calls `kheap_init(SAMOS_HEAP_SIZE_BYTES)`, prints heap size/used/free after L15's KBC token. |

## How we verified

`tests64/L16-dynamic-kheap.sh` checks the three exact values
**and** asserts `used + free == size` (self-consistency that
survives future refactors of the paging mapper).

Observed on VGA:

```
heap size: 104857600
heap used: 839680
heap free: 104017920
```

### Arithmetic check

100 MiB = `104857600` bytes. Block size is 4096. `heap_total_size`
multiplies the table's total entries by block size:
`25600 * 4096 = 104857600`. Matches.

`heap_used = 839680 = 205 * 4096`. Counting the allocations made
up to the print:

| Allocation | Blocks |
|---|---|
| `kmalloc(50)` for `data` | 1 |
| `paging_desc_new`: `kzalloc(struct paging_desc)` | 1 |
| `paging_desc_new`: `paging_pml4_entries_new` -> 4 KiB PML4 | 1 |
| `paging_map_range` walks 400 MiB; needs 1 PDPT, 1 PD, 200 PTs | 202 |
| **Total** | **205** |

`205 * 4096 = 839680`. Matches.

`heap_free = 104857600 - 839680 = 104017920`. Matches.

The L15 regression (`Hello 64-bit!`, ABC, MBC, KBC) still passes.

## Forward look

L18 builds the E820 memory-map probe so we know which physical
regions are usable. L20..L30 turns the single kernel heap into a
multi-heap whose regions are derived from E820. The `kheap_get`,
`heap_total_*`, and `panic` plumbing we added in L16 is the
foundation for that.

Lecture 17 is skipped in the upstream commit history (the next
commit is L18). We follow upstream's numbering, so the next
SamOs commit will be `l18:`.
