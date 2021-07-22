#!/bin/bash
# tests/13-kmalloc-works.sh
#
# Ch 48 - kernel_main runs a kmalloc smoke probe after kheap_init: two
# distinct allocations (km1=100B, km2=8000B), then kfree(km1), then a
# third allocation of the same size as km1. The probe prints each
# returned address in hex. Expectations:
#   km1 starts at SAMOS_HEAP_ADDRESS  (0x01000000)
#   km2 is at km1 + one block         (0x01001000)
#   km3 reuses km1's slot after free  (0x01000000)

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log"

chars=$(cat "$log")

ok=1
# After Ch 51 paging: 1025 blocks (1 dir + 1024 tables).
# Ch 75 added fat16_resolve (9 persistent blocks for the bigger root dir
# and streams) and the fopen/fread chain.
# Ch 81 now calls fclose which releases 4 blocks (fat_item, cloned dir
# item, fat_file_descriptor, the VFS file_descriptor).
# First free slot when kmalloc smoke runs is 1037:
#   0x01000000 + 1037 * 0x1000 = 0x0140D000
echo "$chars" | grep -q 'km1=01411000' || { echo "FAIL: km1 != 0x01411000"; ok=0; }
echo "$chars" | grep -q 'km2=01412000' || { echo "FAIL: km2 != 0x01412000"; ok=0; }
echo "$chars" | grep -q 'km3=01411000' || { echo "FAIL: km3 != 0x01411000 (free+realloc reuse)"; ok=0; }

if [ $ok -ne 1 ]; then
    echo "      first 600 chars: $(echo "$chars" | head -c 600)"
    exit 1
fi

exit 0
