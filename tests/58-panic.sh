#!/bin/bash
# tests/58-panic.sh
#
# Ch 82 - behavioural test for kernel panic().
#
# This is the only test that needs a different kernel image. With
# EXTRA_CFLAGS=-DKERNEL_TEST_PANIC build.sh emits a kernel whose
# kernel_main hits `panic("PANIC-TEST-OK\n")` right before reaching
# userland. We then assert:
#   1) the panic message reaches serial (proves print() runs from
#      inside panic AND that panic returns to the spin loop without
#      crashing), and
#   2) no userland marker (Testing!/Abc!/SamOs) appears after, proving
#      panic actually halts the kernel rather than just printing
#      and returning.
#
# After the test reruns the normal build so subsequent suite runs
# see the standard image.

set -e
cd "$(dirname "$0")/.."

log=$(mktemp)
trap 'rm -f "$log"; bash ./build.sh > /dev/null 2>&1 || true' EXIT

EXTRA_CFLAGS=-DKERNEL_TEST_PANIC bash ./build.sh > /dev/null

( sleep 2; printf 'quit\n' ) | timeout 10 qemu-system-x86_64 \
        -hda bin/os.bin \
        -m 256 \
        -accel tcg \
        -display none \
        -vga std \
        -serial file:"$log" \
        -monitor stdio \
        -no-reboot \
        > /dev/null 2>&1

grep -q 'PANIC-TEST-OK' "$log" || {
    echo "FAIL: 'PANIC-TEST-OK' not in serial - panic message did not reach print()"
    head -30 "$log"
    exit 1
}

# After the panic marker the kernel should be spinning in panic's
# `while(1){}`. No userland prints can appear.
post=$(awk '/PANIC-TEST-OK/{found=1; next} found' "$log")
if echo "$post" | grep -qE 'Testing!|Abc!|SamOs|CRASH-RAN'; then
    echo "FAIL: userland markers appeared AFTER 'PANIC-TEST-OK' - panic() did not actually halt"
    echo "      post-panic excerpt: $(echo "$post" | head -c 200)"
    exit 1
fi

exit 0
