#!/bin/bash
# tests/10-vga-prints-hello.sh
#
# Ch 38 - kernel_main initializes the VGA terminal and prints
# "Hello world!\ntest". Verify both strings show up in VGA text RAM.
#
# Same VGA-buffer-grep approach as the early-real-mode tests.

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log"

chars=$(cat "$log")

ok=1
echo "$chars" | grep -q 'Hello world!' || { echo "FAIL: 'Hello world!' not in VGA buffer"; ok=0; }
echo "$chars" | grep -q 'test'         || { echo "FAIL: 'test' not in VGA buffer";          ok=0; }

if [ $ok -ne 1 ]; then
    echo "      first 300 chars: $(echo "$chars" | head -c 300)"
    exit 1
fi

exit 0
