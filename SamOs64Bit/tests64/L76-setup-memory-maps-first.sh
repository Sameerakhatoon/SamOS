#!/bin/bash
# tests64/L76-setup-memory-maps-first.sh
#
# Lecture 76 - SetupMemoryMaps moved before the kernel
# AllocatePages.
#
# Reason: SetupMemoryMaps's own AllocatePages at
# SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION = 0x210000 races
# with the kernel's AllocatePages at SAMOS_KERNEL_LOCATION =
# 0x100000. Whichever runs second can fail if the first one
# scattered its bookkeeping into a region the second wanted.
# Running SetupMemoryMaps first means the dump area is
# definitively claimed before the kernel allocation happens.

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

SAMOS_C=../SamOs.c

# SetupMemoryMaps must appear BEFORE AllocatePages(...KernelBase)
# in the source.
setup_line=$(grep -n 'SetupMemoryMaps();' "$SAMOS_C" | head -1 | cut -d: -f1)
# The first &KernelBase passed to AllocatePages = the kernel-load
# AllocatePages call.
alloc_line=$(grep -n '&KernelBase' "$SAMOS_C" | head -1 | cut -d: -f1)

[ -n "$setup_line" ] || { echo "FAIL: SetupMemoryMaps call missing"; exit 1; }
[ -n "$alloc_line" ] || { echo "FAIL: kernel AllocatePages missing"; exit 1; }

if [ "$setup_line" -gt "$alloc_line" ]; then
    echo "FAIL: SetupMemoryMaps at line $setup_line is AFTER kernel AllocatePages at line $alloc_line"
    exit 1
fi

[ -f bin/kernel.bin ] || { echo "FAIL: bin/kernel.bin missing"; exit 1; }
exit 0
