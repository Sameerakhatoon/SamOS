# Ch 55 - Implementing paging

Book Ch 51: build the 32-bit paging structures, install them in CR3, and
flip CR0.PG to turn paging on. After this, every virtual address the CPU
sees goes through the page directory + page tables before it hits the bus.

## What got added

- `src/memory/paging/paging.h` - page-flag constants
  (`PAGING_IS_PRESENT`, `PAGING_IS_WRITEABLE`, `PAGING_ACCESS_FROM_ALL`,
  cache/write-through), `PAGING_TOTAL_ENTRIES_PER_TABLE = 1024`,
  `PAGING_PAGE_SIZE = 4096`, and the `struct paging_4gb_chunk` handle
  that owns one identity-mapping page directory.
- `src/memory/paging/paging.c` - `paging_new_4gb(flags)` allocates one
  page directory and 1024 page tables out of the kernel heap and fills
  every PTE with `(phys | flags)` so virtual N maps to physical N.
  `paging_switch(dir)` stashes the directory pointer (the asm helper
  actually loads CR3). `paging_4gb_chunk_get_directory(chunk)` is a
  trivial accessor used by `kernel_main` so the caller never touches
  the struct internals.
- `src/memory/paging/paging.asm` - two tiny wrappers,
  `paging_load_directory` (writes CR3) and `enable_paging` (sets bit 31
  of CR0). Both follow the cdecl-ish prologue the rest of the kernel
  uses.
- `src/memory/heap/kheap.{h,c}` - new `kzalloc(size)` that does
  `kmalloc` + `memset(0)`. Paging needs zeroed memory because any
  unfilled PTE has to read as not-present.
- `src/kernel.c` - call sequence in `kernel_main` is now
  `kheap_init -> idt_init -> paging_new_4gb -> paging_switch ->
  enable_paging -> enable_interrupts`. Paging is set up before
  interrupts are unmasked so any future fault handler can rely on the
  address space being live.

## Why 1025 heap blocks before anything else runs

The directory is one 4 KiB block; each of the 1024 page tables is
another 4 KiB block. That's 1025 blocks consumed before user code ever
calls `kmalloc`. Visible in test 13: the smoke probe now lands at
`0x01402000` (heap start `0x01000000` + `1025 * 0x1000`) instead of the
original `0x01000000`.

## QEMU timing knock-on

Each `kzalloc` here also `memset`s 4 KiB. 1025 of those in QEMU TCG runs
~7 seconds wall-clock on this box. Most tests boot, send monitor
commands, then dump VGA RAM; their previous `sleep 2` was no longer
enough to land after `paging_new_4gb` returned. All tests in the
`05`..`13` range bumped to `sleep 7` (test 11 to 8, with a longer
post-sendkey wait, because a single-key send is more timing-sensitive
than the 3-key version), `timeout 25`, and `-m 256` so the heap pool at
`0x01000000` is comfortably inside RAM.

## How test 14 confirms paging is live

`tests/14-paging-enabled.sh` runs the kernel under QEMU's monitor,
issues `info registers`, and asserts:

- `CR0` has bit 31 set (PG=1)
- `CR3` is non-zero and points above `0x01000000` (i.e. into the heap,
  where `kzalloc` would have placed the page directory).

If either fails, paging is either not enabled or the directory address
got clobbered.
