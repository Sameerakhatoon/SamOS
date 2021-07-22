#!/bin/bash
# tests/32-fat16-read.sh
#
# Ch 75/77 - the FAT16 driver's read + seek are wired into the VFS.
# kernel_main opens /hello.txt, fseeks 2 bytes forward (SEEK_SET=2),
# then freads 11 bytes. hello.txt is "Hello world!\n" so the post-seek
# read should give us "llo world!\n", printed as "hs=llo world!".

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log"

chars=$(cat "$log")

if echo "$chars" | grep -q 'hs=llo world!'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'hs=llo world!'"
echo "      first 1200 chars: $(echo "$chars" | head -c 1200)"
exit 1
