# Lecture 32 - call multiheap_ready

**Source commit (PeachOS64BitCourse):** `2796125`
**SamOs commit:** L32 on `module1-64bit` branch
**Regression test:** `tests64/L32-multiheap-ready.sh`

## Why this chapter exists

L27 wrote `multiheap_ready`. L30 wired it through the rest of
the multi-heap machinery (predicates, defragment-by-paging
second pass). L32 finally CALLS it.

Two tiny edits:

1. `kheap_post_paging()` in kheap.c - a one-liner wrapper that
   calls `multiheap_ready(kernel_multiheap)`.
2. `kernel_main` drops the L23-style drain + "Memory wasted"
   print and instead calls `kheap_post_paging` after
   `paging_switch`.

The whole multi-heap stack goes live with this commit:

- Every E820 type-1 sub-heap above the minimal heap now has a
  shadow at `max_end_data_addr + physical_addr`.
- The PML4 has not-present entries reserved for every virtual-
  arena range.
- The sub-heap set is locked - further `multiheap_add` calls
  fail with `-EINVARG`.
- `kfree` is still a no-op (multiheap_free isn't wired to it
  yet), but `kpalloc` / `kpzalloc` can now use the second-pass
  defragment-by-paging allocator.

## Order matters

```
kheap_init();              // build minimal heap + add E820 sub-heaps
paging_desc_new();         // build kernel PML4
paging_map_e820_regions(); // identity-map every type-1 E820 region
paging_switch();           // load CR3 with the new PML4
kheap_post_paging();       // multiheap_ready
```

`multiheap_ready` panics if no paging descriptor is loaded.
Earlier in the boot we had only the kernel.asm-built identity
map (which works but is not a "real" paging_desc). The switch
to the kernel-owned PML4 happens just before this call.

## SamOs deviation

The drain loop + "Memory wasted" print are gone from
`kernel_main`. Tests up to L23 asserted that token; from L32
onward the regression token is "multiheap ready". The old
tests still exist in `tests64/` as historical record.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/memory/heap/kheap.c` | new `kheap_post_paging()` - one-liner that calls `multiheap_ready(kernel_multiheap)`. |
| `src/memory/heap/kheap.h` | prototype. |
| `src/kernel.c` | remove the L23 drain + "Memory wasted". Call `kheap_post_paging()` after `paging_switch`. Print "multiheap ready" so the regression test has a fresh smoke token. |

## How we verified

VGA after L32:

```
Hello 64-bit!
e820 total: 267910144
ABCmultiheap ready
```

"multiheap ready" appearing proves:

- `paging_switch` worked.
- `multiheap_ready` survived the per-sub-heap shadow build (each
  one allocates a heap_table + bitmap + struct heap from the
  starting heap, then identity-maps the virtual range).
- `paging_map_to(... , 0)` reserves the virtual range without
  faulting.
- `heap_callbacks_set` wires the unmap free callback.
- No `panic("You must have paging setup")` trigger.

## Forward look

L33 begins the IO restoration arc. The original 32-bit IO layer
talked to ports through 16-bit-only `outb` / `inb` wrappers in
io.asm - we need 64-bit versions. After IO, L34 grows the
boot-time sector count again to accommodate the upcoming IDT
and process code; L35 - L38 rebuilds the IDT in 64-bit; L39+
goes on to the user-mode and task subsystems.
