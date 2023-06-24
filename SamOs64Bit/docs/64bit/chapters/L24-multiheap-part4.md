# Lecture 24 - multi-heap part 4

**Source commit (PeachOS64BitCourse):** `a022273`
**SamOs commit:** L24 on `module1-64bit` branch
**Regression test:** `tests64/L24-bitmap-sizing.sh`

## Why this chapter exists

L20 reserved a fixed 1 MiB at the start of the chosen E820
region for the heap bitmap (the `SAMOS_MINIMAL_HEAP_TABLE_SIZE`
constant). That was a conservative guess. With a region of
~256 MiB the actual bitmap (1 byte per 4 KiB block) needs
~64 KiB - we were wasting ~960 KiB. With a region of ~10 GiB
the bitmap would NEED ~2.5 MiB and 1 MiB would underflow.

L24 computes the bitmap size at runtime to fit the actual data
pool exactly.

Two ergonomic side-changes ride along:

1. `SAMOS_HEAP_SIZE_BYTES` -> `SAMOS_HEAP_MINIMUM_SIZE_BYTES`.
   The constant is now a MINIMUM the chosen E820 region must
   meet, not the cap on the minimal heap's size. Renamed to
   match the semantics.
2. `heap_align_value_to_upper` un-staticed and a new symmetric
   `heap_align_value_to_lower` added, both declared in
   `heap.h`. Used by the multiheap defragmenter in a later
   lecture.

## Theory primer: two-pass bitmap sizing

The bitmap and the data pool share one physical region. Both
have to fit:

```
[heap_table_address ......... heap_address ......... end_address]
|<--- bitmap (N entries) --->|<--- data (N * 4 KiB) ----------->|
```

The constraint is "bitmap size = entries * sizeof(entry) =
1 * N bytes, and data size = N * 4 KiB, and bitmap + data =
total_size". Solving:

```
N * 1 + N * 4096 = total
N * 4097 = total
N = total / 4097
```

So technically a single divide gives the answer. But upstream
PeachOS64 uses a two-pass approximation that's a hair more
intuitive and only loses one block of efficiency:

```c
pass 1:  blocks_est       = total_size / 4096           // ignores bitmap overhead
         table_est        = blocks_est * 1
pass 2:  data_size        = total_size - table_est       // shrink data by the est
         data_blocks      = data_size / 4096             // recount actual
         table_final      = data_blocks * 1
         heap_address     = table_address + table_final
         heap_end         = end_address
```

In numbers, for a 256 MiB region:
- pass 1: blocks_est = 65536, table_est = 65536 bytes (64 KiB)
- pass 2: data_size = 256 MiB - 64 KiB = ~255.94 MiB,
          data_blocks = 65520, table_final = 65520 bytes
- bitmap ends at table_addr + 65520
- data pool starts there, runs to end_address

Difference from the exact formula is 1 block (`N - N_exact`).
Negligible.

## Why heap_align_value_to_lower is added now

L24 itself does not call it (the minimal heap region addresses
are already block-aligned because the bitmap is a whole-byte
multiple). But the multiheap-defragment second pass (later
lecture) needs to find the largest aligned sub-region inside a
fragment, and that requires both round-up (for the start of the
range) and round-down (for the end). The header gets it now so
no future lecture has to grow the API while doing more
interesting work.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/config.h` | `SAMOS_HEAP_SIZE_BYTES` renamed to `SAMOS_HEAP_MINIMUM_SIZE_BYTES`. Comment updated. |
| `src/memory/heap/heap.c` | `heap_align_value_to_upper` un-staticed. New `heap_align_value_to_lower`. Forward decl updated. |
| `src/memory/heap/heap.h` | both alignment helpers exposed. |
| `src/memory/heap/kheap.c` | macro rename. Two-pass bitmap sizing replaces the fixed `SAMOS_MINIMAL_HEAP_TABLE_SIZE` constant. The data pool start is now `table_address + computed_table_size`, not `table_address + 1 MiB`. |

## How we verified

VGA after L24:

```
Hello 64-bit!
e820 total: 267910144
ABCMemory wasted
```

Same tokens as L23. The internal layout changed but the user-
visible behaviour is identical: paging_switch survives, drain
loop reaches OOM, "Memory wasted" prints.

What's quietly different: the data pool is now about 1 MiB
larger than at L23 (we recovered most of the wasted bitmap
budget) and the bitmap is now correctly sized for whatever
region we picked instead of being capped at 1 MiB.

## Forward look

L25 - L30 continue the multiheap (free-by-range, callbacks,
type queries, the paging-defragment second pass). L31 implements
paging_get.
