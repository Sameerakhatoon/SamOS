#!/bin/bash
# tests/49-putchar.sh
#
# Ch 109 - behavioural test for the user-mode putchar syscall (cmd 3
# SYSTEM_COMMAND3_PUTCHAR).
#
# kernel_main loads a blank.elf instance with argv[0]="PC". Its main
# prints "PC-" via the bulk-print syscall, then sends 'X', 'y', 'z',
# '\n' through samos_putchar (one int 0x80 each). The expected serial
# stream is "PC-Xyz\n".

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log" 'PC-' 'Abc!'

# PIT can preempt the PC task BETWEEN the print("PC-") call (one
# cmd 1 syscall) and each of the four samos_putchar calls (one cmd 3
# each), so the bytes can be interleaved with other tasks' output.
# Strip out everything that isn't part of the PC sequence to verify
# the four cmd 3 putchar calls all reached the kernel and were emitted.
seq=$(grep -aoE '(PC-)|[Xyz]' "$log" | tr -d '\n' | sed -n 's/.*PC-\([Xyz]*\).*/PC-\1/p')

case "$seq" in
    *PC-Xyz*)
        exit 0
        ;;
esac

echo "FAIL: cmd 3 putchar did not emit X+y+z after PC-"
echo "      stripped sequence: $seq"
echo "      first 400 bytes of log:"; head -c 400 "$log"
exit 1
