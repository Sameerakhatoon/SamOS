# Lecture 21 - paging-map present-only E820 memory

**Source commit (PeachOS64BitCourse):** `c346a33`
**SamOs commit:** L21 on `module1-64bit` branch
**Regression test:** `tests64/L21-paging-map-e820.sh`

## Why this chapter exists

L13 identity-mapped a fixed 400 MiB blob into a fresh paging
descriptor. That was a placeholder. The correct behavior is to
only mark E820-usable regions as present, leaving reserved and
MMIO regions unmapped so a stray pointer into them faults
immediately rather than silently working.

L21 adds `paging_map_e820_memory_regions(desc)`. It walks every
E820 entry, filters to `type == 1` (usable), aligns the bounds
to page boundaries, and calls `paging_map_to` to install each
range into the descriptor.

## Theory primer

### Alignment direction matters

Each E820 entry's `base_addr` and `length` are byte-granular. Page
mapping is page-granular. We need integer-page bounds, so:

- `base_addr` rounded UP to the next page boundary (via
  `paging_align_address`).
- `end_addr` rounded DOWN to the current page boundary (via
  `paging_align_to_lower_page`).

The asymmetry is deliberate. Rounding `base` UP excludes any
sub-page leftover at the start (we cannot map half a page); same
for `end` DOWN. The skipped bytes were usable RAM but the cost
of mapping a whole page that overlaps a reserved region is
worse: a wild pointer there would silently succeed.

### `type == 1` only

E820 has multiple types:
- 1: usable
- 2: reserved
- 3: ACPI reclaim
- 4: ACPI NVS
- 5: bad memory

We only map type 1. ACPI reclaim is technically reusable AFTER
the OS reads ACPI tables (which we do not yet), so we
conservatively skip it. Reserved and bad obviously stay unmapped.

### The deferred-bug situation

L20 stubbed `kzalloc` to `return NULL` pending `multiheap_zalloc`
in a later lecture. `paging_desc_new` calls `kzalloc` to allocate
the descriptor and the PML4 table:

```c
struct paging_desc* paging_desc_new(paging_map_level_t level) {
    struct paging_desc* desc = kzalloc(sizeof(struct paging_desc));
    if (!desc) return NULL;
    desc->pml = paging_pml4_entries_new();   // also kzalloc
    ...
}
```

So at L21 head, `paging_desc_new` returns NULL. Upstream
PeachOS64 commits the call to `paging_map_e820_memory_regions`
unguarded. We add a NULL guard in `kernel.c` because

1. The boot-time identity map covers only the first 1 GiB. The
   nonsense "pml" pointer that NULL-deref would compute is
   almost certainly above 1 GiB - immediate `#PF`.
2. We want the regression test to remain green between L21 and
   the lecture that fixes kzalloc, not just at the end of the
   arc.

The guard is two lines and disappears (no-op effect) once kzalloc
returns real memory.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/memory/paging/paging.c` | new `paging_map_e820_memory_regions(desc)`. Walks E820, filters to type 1, aligns base/end, calls `paging_map_to`. |
| `src/memory/paging/paging.h` | adds prototype. |
| `src/kernel.c` | builds `kernel_paging_desc` and calls `paging_map_e820_memory_regions`. NULL-guarded so the kzalloc stub does not crash us. Prints one of two L21 marker strings depending on whether kzalloc returned. |

## How we verified

VGA after L21:

```
Hello 64-bit!
e820 total: 267910144
ABCL21 paging desc NULL (kzalloc stub)
```

The "L21 paging desc NULL" line is the deferred-bug marker - it
proves the guard ran (so kernel_main did NOT crash) and that
kzalloc is in fact still stubbed (so we have not silently
regressed L20's accepted-bug). Once a later lecture wires
multiheap_zalloc through, this becomes "L21 paging map built".

The test asserts banner + e820 line + ABC + either of the two
marker strings, so it stays green across the stub-to-real
transition without needing edits.

## Forward look

L22 starts adding the per-heap zalloc / free machinery. Some
later lecture in the multi-heap arc will route kzalloc through
multiheap_zalloc, at which point this lecture's code path will
exercise the real walk. The same lecture should then probably
paging_switch to the new descriptor (currently commented out in
both upstream and SamOs).
