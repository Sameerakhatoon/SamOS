# Lecture 20 - building the multi-heap, part 1

**Source commit (PeachOS64BitCourse):** `c83851c`
**SamOs commit:** L20 on `module1-64bit` branch
**Regression test:** `tests64/L20-multiheap.sh`

## Why this chapter exists

L16 made the kernel heap dynamically-sized but it was still ONE
heap. Real physical memory is fragmented: BIOS reserves the low
640 KiB, ROM lives at 0xC0000, ACPI tables are scattered, and on
big systems the usable RAM comes in multiple chunks (e.g. a
chunk below 4 GiB and another above). L18 taught us how to
discover that with E820. L20 wires E820 into the heap layer: one
kheap-shaped facade backed by a linked list of `struct heap`
sub-regions, each carved out of a single E820 usable range.

This is the "Part 1 of 9" - it gets the structure in place
(bootstrap, registration, first-pass allocation). Defragmenting
via paging, the zalloc/free path, type-routing for callbacks,
and the get/find APIs land in L21..L31.

## Theory primer

### The bootstrap problem

A multiheap is itself a `struct multiheap*` plus N
`struct multiheap_single_heap*` nodes plus, for each
dynamically-added sub-heap, a `struct heap*` and a
`struct heap_table*`. That's a non-trivial amount of memory.
Where do those allocations live BEFORE any heap exists?

Answer: the "minimal heap" pattern.

1. The kernel builds one heap by hand - the **minimal heap**.
   Its header and bitmap live at fixed kernel addresses
   (`SAMOS_MINIMAL_HEAP_TABLE_ADDRESS` = 16 MiB for the bitmap,
   `SAMOS_MINIMAL_HEAP_ADDRESS` = 17 MiB for the data pool). Its
   size is `SAMOS_HEAP_SIZE_BYTES` (100 MiB).
2. The minimal heap is the `starting_heap` of the multiheap.
   Every dynamic allocation the multiheap layer needs (its own
   header, every per-region node, every dynamically-built
   sub-heap header/table) comes from the minimal heap.
3. The minimal heap itself is registered into the multiheap with
   the `EXTERNALLY_OWNED` flag, so `multiheap_free` knows not to
   try heap_free-ing it back into itself.

This is the same bootstrap pattern used by malloc implementations
for arena/page metadata. The trick is that the FIRST heap has
no parent and lives at a known-good fixed address.

### Picking the minimal heap's region

`kheap_get_allowable_region_for_minimal_heap` walks E820 looking
for the first `type == 1` entry with `length > SAMOS_HEAP_SIZE_BYTES`.
Under QEMU `-m 256` that's the big chunk above 1 MiB. If no
suitable region exists we panic - the multiheap cannot bootstrap.

The bitmap address is clamped UP to `SAMOS_MINIMAL_HEAP_TABLE_ADDRESS`
so the bitmap never lands on the kernel image (which is loaded
at 1 MiB by boot.asm) or on the E820 buffer at 0x7E00 (which is
now preserved across `kheap_init` because the bitmap moved).

### First-pass allocation

`multiheap_alloc` walks the linked list of `multiheap_single_heap*`
nodes and calls `heap_malloc` on each in order. First non-null
wins. There is no "best fit" or "minimize fragmentation" logic
yet - that lands in the second-pass paging-defragment alloc
later in the arc.

`multiheap_palloc` is the same first-pass, then falls through to
`multiheap_alloc_second_pass` (currently a NULL stub - the
defragmenter shows up in a later lecture). Documenting the API
shape early lets future callers pick which path they want without
later refactoring.

### kzalloc / kfree stubs

Upstream L20 reduces `kzalloc` to `return NULL;` and `kfree` to a
no-op. SamOs follows. Rationale: the multiheap layer does not yet
have `multiheap_zalloc` or `multiheap_free` (free has to figure
out which sub-heap the pointer belongs to via the new `eaddr`
field, and zalloc has to choose the sub-heap before memset).
Patching both this lecture AND the multiheap in one go would
make the commit too big - the deferred TODOs are explicit so the
follow-up lectures can be small.

Practical consequence: any code that calls `kzalloc` or `kfree`
between L20 and the patch lectures is broken. In our case the
only callers are `paging.c` (paging_desc_new etc) - we have
removed the paging probe from `kernel_main`, so the path is
quiescent. The first time a future lecture re-enables paging
operations we will likely need to lift the stubs.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/config.h` | drop `SAMOS_HEAP_ADDRESS`, `SAMOS_HEAP_TABLE_ADDRESS`. Add `SAMOS_MINIMAL_HEAP_TABLE_ADDRESS` (0x01000000), `SAMOS_MINIMAL_HEAP_ADDRESS` (0x01100000), `SAMOS_MINIMAL_HEAP_TABLE_SIZE`. |
| `src/memory/memory.h` + `.c` | add `e820_total_entries()` and `e820_entry(index)`. Existing helpers use them for code reuse. |
| `src/memory/heap/heap.h` | `struct heap` gets `void* eaddr`. New `heap_zalloc` prototype. |
| `src/memory/heap/heap.c` | `heap_create` populates `eaddr`. `heap_zalloc` implementation. |
| `src/memory/heap/multiheap.h` (NEW) | `MULTIHEAP_HEAP_FLAG_EXTERNALLY_OWNED`, `MULTIHEAP_HEAP_FLAG_DEFRAGMENT_WITH_PAGING`, `struct multiheap_single_heap`, `struct multiheap`, and the public API. |
| `src/memory/heap/multiheap.c` (NEW) | `multiheap_new`, `multiheap_add`, `multiheap_add_existing_heap`, `multiheap_alloc` (first-pass), `multiheap_palloc`, `multiheap_free`, plus internal helpers. |
| `src/memory/heap/kheap.h` | `kheap_init` back to no-args. `kheap_get` removed from header (still defined in C as a no-op carryover - unused now). |
| `src/memory/heap/kheap.c` | rewritten on top of multiheap. Picks the minimal heap region from E820, registers it as starting_heap, then adds every other type-1 E820 region as a sub-heap. `kzalloc` / `kfree` reduced to stubs. |
| `src/kernel.c` | removes the L13/L15/L16 probes (paging_desc, kernel_page, heap accounting). Just banner + e820 total + kheap_init + kmalloc(50)/ABC + wait loop. |
| `Makefile` | adds `multiheap.o` to FILES and a build rule. |

## How we verified

VGA after L20:

```
Hello 64-bit!
e820 total: 267910144
ABC
```

ABC proves:
- `kheap_init` survived (E820 walk, minimal heap creation,
  multiheap setup, additional E820 region registration).
- `kmalloc(50)` routed through `multiheap_alloc_first_pass`,
  found space in the minimal heap, returned a valid pointer.
- The minimal heap is correctly placed at 0x01100000 (kernel
  image at 1 MiB does not get clobbered by the bitmap).

The L13/L15/L16 regression tokens (`MBC`, `KBC`, `heap size:`
etc) are gone by design - upstream removed those probes here.
The old tests still exist under `tests64/` but no longer apply
to the head of `module1-64bit`. They remain as documentation of
what those lectures introduced.

## Known stubs

| Stub | Will land in |
|---|---|
| `kzalloc` returns NULL | future lecture wiring `multiheap_zalloc` |
| `kfree` no-op | future lecture wiring `multiheap_free` |
| `multiheap_alloc_second_pass` returns NULL | future lecture wiring paging-based defragment |

## Forward look

L21 starts using `e820` data to drive a paging map of present-only
memory (the paging probe in `kernel_main` will likely reappear in
some form). L22..L30 fills in the multiheap details:
zalloc/free, defragment, callback-based ownership, type routing.

Multi-heap is a 9-lecture arc; this is part 1.
