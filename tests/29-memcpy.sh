#!/bin/bash
# tests/29-memcpy.sh
#
# Ch 71 - memcpy copies len bytes from src to dest. kernel_main copies
# "abc" into a zeroed stack buffer and then prints strlen(dest) as hex.
# Expected: "mcp=00000003".

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log"

chars=$(cat "$log")

if echo "$chars" | grep -q 'mcp=00000003'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'mcp=00000003'"
echo "      first 1000 chars: $(echo "$chars" | head -c 1000)"
exit 1
