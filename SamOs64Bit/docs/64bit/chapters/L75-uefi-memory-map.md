# Lecture 75 - UEFI memory map as E820

**Source commit (PeachOS64BitCourse):** `14eb84c`
**SamOs commit:** L75 on `module1-64bit` branch
**Regression test:** `tests64/L75-uefi-memory-map.sh`

## Why this chapter exists

The kernel's `memory.c` reads E820 entries via two macros:
`SAMOS_MEMORY_MAP_LOCATION` (the entries) and
`SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION` (the count). Under
BIOS, those addresses were `0x7E00 / 0x7DFE` - the bottom of
the boot sector area where `boot.asm` dumped int 0x15 / 0xE820
results.

Under UEFI:
- int 0x15 doesn't exist
- The boot sector area is reserved for the firmware itself

L75 has the UEFI bootloader query `gBS->GetMemoryMap` and
re-emit the result in E820 format at a UEFI-friendly address
(`0x210000`). Two layout changes from the BIOS shape:

1. Addresses move: `0x7DFE/0x7E00` -> `0x210000/0x210008`.
2. The count is now `UINT64` (was `uint16_t` at `0x7DFE`).
   The 8-byte gap between count and entries replaces the
   2-byte gap.

## The two-pass GetMemoryMap

```c
gBS->GetMemoryMap(&memoryMapSize=0, NULL, &mapKey,
                  &descriptorSize, &descriptorVersion);
// Returns EFI_BUFFER_TOO_SMALL with memoryMapSize updated.

memoryMapSize += descriptorSize * 10;        // headroom
memoryMap = AllocatePool(memoryMapSize);

gBS->GetMemoryMap(&memoryMapSize, memoryMap, &mapKey,
                  &descriptorSize, &descriptorVersion);
```

The first call with NULL returns the size needed; the
caller allocates and queries again. The 10-descriptor padding
accounts for the fact that `AllocatePool` itself can change
the memory map (adding descriptors for its own bookkeeping),
so pass 2 might come back with more entries than pass 1 sized
for.

## Filtering for EfiConventionalMemory

UEFI memory types include:
- EfiReservedMemoryType
- EfiLoaderCode / EfiLoaderData (our .efi)
- EfiBootServicesCode / EfiBootServicesData (gBS internals)
- EfiRuntimeServicesCode / EfiRuntimeServicesData (gRT)
- EfiConventionalMemory (= usable RAM, what we want)
- EfiUnusableMemory (= bad RAM)
- EfiACPIReclaimMemory (= reclaim after ACPI tables read)
- EfiACPIMemoryNVS (= preserve through suspend)
- EfiMemoryMappedIO / EfiMemoryMappedIOPortSpace
- EfiPalCode

We treat only `EfiConventionalMemory` as type=1 (BIOS E820's
"usable"). Reclaim categories are skipped pending ACPI table
support.

## Kernel side: unchanged

The kernel's `e820_total_entries` and `e820_entry` already
read via the two macros. Updating the macros to the new
addresses is all that's needed on the kernel side. The count
is still read as `uint16_t` - safe as long as the count fits
in 16 bits, which it always does (real systems have at most
~30 usable regions).

## SamOs vs upstream

Identical structurally. Just the SamOs_ vs PEACHOS_ prefix
swap on the config macros.

## How CI verifies

Source-check only (no EDK2 build):
- E820Entry and E820Entries typedefs in SamOs.c
- SetupMemoryMaps function present
- GetMemoryMap call present
- EfiConventionalMemory filter present
- config.h macros at the new addresses

## Forward look

L76 - solve the "unmapped kernel" problem. The kernel's
paging code builds new PML4s from scratch via paging_desc_new
+ paging_map_e820_memory_regions; the latter relies on the
fresh map at 0x210000 we just wrote. But the kernel's OWN
code at 0x100000+ has to be mapped in too or the next
instruction faults. L76 fixes that.
