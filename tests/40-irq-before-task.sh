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
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log"

chars=$(cat "$log")

if echo "$chars" | grep -q 'bootsig=000055AA'; then
    exit 0
fi

echo "FAIL: kernel triple-faulted on first IRQ - VGA missing 'bootsig=000055AA' marker"
echo "      first 300 chars: $(echo "$chars" | head -c 300)"
exit 1
