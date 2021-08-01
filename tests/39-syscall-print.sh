#!/bin/bash
# tests/39-syscall-print.sh
#
# Ch 108 - SYSTEM_COMMAND1_PRINT round-trip from ring 3 to VGA + COM1.
# The user task pushes a string pointer, int 0x80 dispatches to
# isr80h_command1_print, which pulls the user pointer via
# task_get_stack_item, copies the string with copy_string_from_task
# (the page-table portal), and writes it through the kernel's print()
# (which now mirrors to COM1 via terminal_writechar).
#
# Several user tasks loaded by the demo issue cmd 1: Testing!/Abc!/
# CRASH-RAN/EXIT-RAN/etc. Any of them landing on serial proves cmd 1
# works.

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log" 'Testing!' 'Abc!'

if grep -qE 'Testing!|Abc!|CRASH-RAN|EXIT-RAN' "$log"; then
    exit 0
fi

echo "FAIL: no cmd 1 print output on serial - print syscall path broken"
echo "      first 400 bytes: $(head -c 400 "$log")"
exit 1
