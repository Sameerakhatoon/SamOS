#!/bin/bash
# tests/17-string-utils.sh
#
# Ch 58 - kernel_main calls strnlen("hello", 100) and prints the result
# as 8 hex digits. The expected line is "slen=00000005". This verifies
# the new src/string/string.c implementation and that kernel.c can call
# into it correctly.

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log"

chars=$(cat "$log")

if echo "$chars" | grep -q 'slen=00000005'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'slen=00000005'"
echo "      first 600 chars: $(echo "$chars" | head -c 600)"
exit 1
