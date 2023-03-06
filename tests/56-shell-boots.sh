#!/bin/bash
# tests/56-shell-boots.sh
#
# Ch 137 - behavioural test for the shell program (programs/shell/).
#
# kernel_main loads shell.elf as the last task. shell.c's main prints
# "SamOs v1.0.0\n" then enters a readline loop emitting "> " before
# each prompt. The serial mirror seeing both proves shell.elf actually
# started, reached its main, AND entered its print-readline loop.

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

# Wait for the shell prompt - which only fires AFTER shell printed the
# banner and reached its readline loop. The marker-sync helper guards
# against wall-clock variation in shell.elf's scheduling.
run_kernel_capture "$log" '> '

grep -q 'SamOs v1.0.0' "$log" || {
    echo "FAIL: 'SamOs v1.0.0' not in serial - shell.elf never reached main"
    head -30 "$log"
    exit 1
}

grep -q '> ' "$log" || {
    echo "FAIL: '> ' not in serial - shell entered main but didn't reach its readline loop"
    head -30 "$log"
    exit 1
}

exit 0
