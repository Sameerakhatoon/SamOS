#!/bin/bash
# tests64/L75-uefi-memory-map.sh
#
# Lecture 75 - UEFI memory map -> E820-shaped dump.
#
# SamOs.c grows SetupMemoryMaps which:
#   1. gBS->GetMemoryMap (twice - first to size, then to fill)
#   2. Counts EfiConventionalMemory descriptors
#   3. AllocatePages(AllocateAddress, EfiLoaderData, ...,
#      &MemoryMapLocationE820 = SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION)
#   4. Writes UINT64 count at offset 0, E820Entry array at
#      offset 8
#
# config.h moves SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION and
# SAMOS_MEMORY_MAP_LOCATION from 0x7DFE/0x7E00 (BIOS boot
# sector) to 0x210000/0x210008 (UEFI AllocatePages area).
#
# Kernel-side e820_total_entries reads the count as uint16_t -
# fine as long as count fits in 16 bits (we have maybe 20
# usable regions on a typical PC).

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

SAMOS_C=../SamOs.c

grep -q 'typedef struct.*E820Entry' "$SAMOS_C" \
    || { echo "FAIL: E820Entry typedef missing"; exit 1; }
grep -q 'typedef struct.*E820Entries' "$SAMOS_C" \
    || { echo "FAIL: E820Entries typedef missing"; exit 1; }
grep -q 'SetupMemoryMaps' "$SAMOS_C" \
    || { echo "FAIL: SetupMemoryMaps missing"; exit 1; }
grep -q 'gBS->GetMemoryMap' "$SAMOS_C" \
    || { echo "FAIL: GetMemoryMap call missing"; exit 1; }
grep -q 'EfiConventionalMemory' "$SAMOS_C" \
    || { echo "FAIL: EfiConventionalMemory filter missing"; exit 1; }

grep -q '^#define SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION  0x210000' src/config.h \
    || { echo "FAIL: SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION not at 0x210000"; exit 1; }
grep -q '^#define SAMOS_MEMORY_MAP_LOCATION                0x210008' src/config.h \
    || { echo "FAIL: SAMOS_MEMORY_MAP_LOCATION not at 0x210008"; exit 1; }

[ -f bin/kernel.bin ] || { echo "FAIL: bin/kernel.bin missing"; exit 1; }
exit 0
