#!/bin/bash
# tests/16-disk-reads-sector.sh
#
# Ch 54 - disk_read_sector reads one 512-byte sector from LBA 0 (the
# boot sector) into a stack buffer. kernel_main prints buf[510..511]
# as a 32-bit hex (top 16 bits 0, bottom 16 bits = the two boot-signature
# bytes). For a valid PC boot sector buf[510]=0x55, buf[511]=0xAA, so
# we expect "bootsig=000055AA" on screen.

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log"

chars=$(cat "$log")

if echo "$chars" | grep -q 'bootsig=000055AA'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'bootsig=000055AA'"
echo "      first 600 chars: $(echo "$chars" | head -c 600)"
exit 1
