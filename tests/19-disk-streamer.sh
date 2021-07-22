#!/bin/bash
# tests/19-disk-streamer.sh
#
# Ch 60 - the disk streamer abstracts sector-grained PIO into byte-grained
# reads. kernel_main creates a stream, seeks to byte 0x1FE (offset 510
# inside sector 0, i.e. the boot signature), reads 2 bytes, and prints
# them as a 32-bit hex. Expected on-screen: "ss=000055AA".

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log"

chars=$(cat "$log")

if echo "$chars" | grep -q 'ss=000055AA'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'ss=000055AA'"
echo "      first 700 chars: $(echo "$chars" | head -c 700)"
exit 1
