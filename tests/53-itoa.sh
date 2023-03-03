#!/bin/bash
# tests/53-itoa.sh
#
# Ch 133 - behavioural test for the stdlib itoa(int).
#
# kernel_main loads a blank.elf instance with argv[0]="IT". Its main
# calls itoa(8763) and prints "IT:" + result + "\n". If itoa drops
# digits, reverses, or produces a NULL, the marker "IT:8763" will not
# appear on the serial mirror.

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log" 'IT:8763' 'Abc!'

grep -q 'IT:8763' "$log" || {
    echo "FAIL: 'IT:8763' not in serial - itoa(8763) did not produce '8763'"
    head -30 "$log"
    exit 1
}
exit 0
