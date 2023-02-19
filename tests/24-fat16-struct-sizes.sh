#!/bin/bash
# tests/24-fat16-struct-sizes.sh
#
# Ch 66 - the FAT16 driver declares the packed C structs that mirror
# on-disk layouts. fat16_check_sizes() returns 1 iff:
#   sizeof(fat_header)          == 36
#   sizeof(fat_header_extended) == 26
#   sizeof(fat_directory_item)  == 32
# kernel_main prints the result; expected on-screen "fszs=00000001".

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log"

chars=$(cat "$log")

if echo "$chars" | grep -q 'fszs=00000001'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'fszs=00000001'"
echo "      first 700 chars: $(echo "$chars" | head -c 700)"
exit 1
