#!/bin/bash
# tests/40-irq-before-task.sh
#
# G05 regression sentinel - after Ch 113, the generic interrupt_handler
# calls task_page() on every IRQ. Before the first user task is loaded,
# current_task is NULL and task_page -> task_switch(NULL) triple-faults
# the kernel. This test asserts the kernel reaches its post-IRQ smoke
# probes (bootsig=000055AA) AFTER enable_interrupts() has run for a few
# seconds, i.e. IRQs are firing and not killing the kernel.
#
# The fix in idt.c wraps task_page() in `if (task_current())`.

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

dump=$(mktemp)
trap 'rm -f "$dump"' EXIT

(
    sleep 8
    printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump"
) | timeout 25 qemu-system-x86_64 \
        -hda bin/os.bin \
        -m 256 \
        -accel tcg \
        -display none \
        -vga std \
        -monitor stdio \
        -no-reboot \
        > /dev/null 2>&1

chars=$(od -An -v -tx1 -w1 "$dump" \
            | awk 'NR%2==1 {printf "%s\n", $1}' \
            | xxd -r -p \
            | tr '\0' ' ')

if echo "$chars" | grep -q 'bootsig=000055AA'; then
    exit 0
fi

echo "FAIL: kernel triple-faulted on first IRQ - VGA missing 'bootsig=000055AA' marker"
echo "      first 300 chars: $(echo "$chars" | head -c 300)"
exit 1
