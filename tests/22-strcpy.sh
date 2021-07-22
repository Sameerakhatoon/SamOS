#!/bin/bash
# tests/22-strcpy.sh
#
# Ch 64 - strcpy("hi") into a stack buffer must produce "hi\0".
# kernel_main prints "scp=hi" followed by strlen as hex; the expected
# line is "scp=hi00000002".

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log"

chars=$(cat "$log")

if echo "$chars" | grep -q 'scp=hi00000002'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'scp=hi00000002'"
echo "      first 700 chars: $(echo "$chars" | head -c 700)"
exit 1
