#!/bin/bash
# tests/23-fat16-registers.sh
#
# Ch 65 - the FAT16 driver stub registers itself with the VFS via
# fs_static_load -> fs_insert_filesystem(fat16_init()). When
# disk_search_and_init calls fs_resolve(&disk), it walks the table,
# calls fat16_resolve (which currently returns 0 for any disk), and
# stores fat16_fs in disk.filesystem. kernel_main prints the name.
# Expected on-screen: "fs=FAT16".

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log"

chars=$(cat "$log")

if echo "$chars" | grep -q 'fs=FAT16'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'fs=FAT16'"
echo "      first 700 chars: $(echo "$chars" | head -c 700)"
exit 1
