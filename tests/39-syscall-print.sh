#!/bin/bash
# tests/39-syscall-print.sh
#
# Ch 108 - SYSTEM_COMMAND1_PRINT exact-string round-trip from ring 3
# to COM1. The user task pushes a literal pointer, int 0x80 dispatches
# to isr80h_command1_print, which pulls the user pointer via
# task_get_stack_item, copies the string out of user space with
# copy_string_from_task (the page-table portal trick), and writes
# every char through the kernel's print() (which mirrors to COM1).
#
# We assert FIVE specific cmd-1 strings all reach serial in order:
#   "Testing!"   - the long-running Testing! task (loop print(argv[0]))
#   "Abc!"       - the long-running Abc! task
#   "CRASH-RAN"  - the short-lived CRASH task (proves cmd 1 fires
#                  even on a soon-to-fault task)
#   "BS-ABC"     - the short-lived BS task (proves print() carries the
#                  full string, including bytes that terminal_writechar
#                  will route to backspace)
#   "EXIT-RAN"   - the short-lived EXIT task
#
# Any miss = either cmd 1 isn't routing correctly, or
# copy_string_from_task truncated, or the COM1 mirror lost bytes.

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

# Wait until both EXIT-RAN (last transient to fire) and the long-running
# Abc! marker have appeared, so all 5 strings should be in the log.
run_kernel_capture "$log" 'EXIT-RAN' 'Abc!'

ok=1
for marker in 'Testing!' 'Abc!' 'CRASH-RAN' 'BS-ABC' 'EXIT-RAN'; do
    grep -q "$marker" "$log" || {
        echo "FAIL: cmd 1 print round-trip missed '$marker'"
        ok=0
    }
done

if [ $ok -ne 1 ]; then
    echo "      log head 30 lines:"
    head -30 "$log"
    exit 1
fi
exit 0
