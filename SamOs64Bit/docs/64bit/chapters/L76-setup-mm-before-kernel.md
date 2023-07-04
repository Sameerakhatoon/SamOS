# Lecture 76 - SetupMemoryMaps runs before kernel allocation

**Source commit (PeachOS64BitCourse):** `8bbe7e7`
**SamOs commit:** L76 on `module1-64bit` branch
**Regression test:** `tests64/L76-setup-memory-maps-first.sh`

## The bug L75 had

L75 placed `SetupMemoryMaps()` BETWEEN the kernel
`AllocatePages` and `ExitBootServices`. That order let the
kernel `AllocatePages` for 0x100000 succeed, then
`SetupMemoryMaps` tried to AllocatePages at 0x210000 - which
could fail if any UEFI internal had picked that region in the
intervening time (UEFI does its own AllocatePool for
bookkeeping every time we touch the memory map, including
during the kernel read).

Upstream calls this "the unmapped kernel problem": the
kernel-side `paging_map_e820_memory_regions` would then walk
empty memory map data and produce a PML4 that doesn't include
the kernel's 0x100000 page. The very first instruction after
the kernel's `paging_switch` would page-fault on its own RIP.

## The fix

```diff
- // After reading kernel and AllocatePages(KernelBase)
- ...
- CopyMem(KernelBase, KernelBuffer, KernelBufferSize);
- // Setup and load E820 Entries
- SetupMemoryMaps();
+ // Setup and load E820 Entries
+ SetupMemoryMaps();
+
+ // ReadFileFromCurrentFilesystem, AllocatePages, CopyMem...
```

Pulling the call ALL THE WAY UP makes the memory dump the
first big AllocatePages we do. The dump's 0x210000 region is
reserved before any of the kernel-load path can step on it.

## Why this matters more than it looks

UEFI's AllocatePool dynamically extends EfiBootServicesData
regions. Each AllocatePool can split or extend a descriptor.
Each `GetMemoryMap` call sees the current state. If we
re-arrange the order, we change what UEFI's memory map looks
like at the moment we dump it.

By running SetupMemoryMaps first:
- The dump is of a "clean" memory map (no fresh
  EfiLoaderData for the kernel yet)
- The dump's AllocatePages claims 0x210000 deterministically
- Subsequent allocations work around it

If we run it last:
- The dump is of the post-kernel-load memory map (more
  fragmented descriptors)
- The dump's AllocatePages might race with whatever the
  kernel CopyMem allocated for its source buffer

## How CI verifies

Source-check: in SamOs.c, the line number of
`SetupMemoryMaps()` must be LESS than the line number of
`AllocatePages(..., &KernelBase)`. The build still succeeds.

## Forward look

L77 - GPT partition table reader. UEFI disks are GPT, our
disk_search_and_init currently grovels in the boot sector
expecting MBR + FAT BPB. L77 parses the GPT primary header
and partition entries.

L78 - virtual disks: each GPT partition becomes a separate
disk handle so the kernel can fopen across multiple
partitions on the same physical disk.
