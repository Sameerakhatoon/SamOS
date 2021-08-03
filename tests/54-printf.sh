#!/bin/bash
# tests/54-printf.sh
#
# Ch 135 - behavioural test for the stdlib printf (variadic, %i + %s).
#
# kernel_main loads a blank.elf instance with argv[0]="PF". Its main
# calls printf("PF:%i=%i %s\n", 42, 42, "hi"). The stdlib's printf is
# built on putchar + print + itoa, so a passing test 54 is also a
# fitness check for all three pieces working together.

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log" 'PF:42=42 hi' 'Abc!'

grep -q 'PF:42=42 hi' "$log" || {
    echo "FAIL: 'PF:42=42 hi' not in serial - printf %i / %s did not format correctly"
    head -30 "$log"
    exit 1
}
exit 0
