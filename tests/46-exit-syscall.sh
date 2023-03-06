#!/bin/bash
# tests/46-exit-syscall.sh
#
# Ch 148 - behavioural test for samos_exit / SYSTEM_COMMAND9_EXIT.
#
# kernel_main loads a blank.elf instance with argv[0]="EXIT". Its main
# prints "EXIT-RAN" then returns. The user-mode c_start (programs/stdlib
# start.c) calls samos_exit() after main returns; samos_exit issues
# int 0x80 cmd 9 which lands in isr80h_command9_exit, which calls
# process_terminate(task_current()->process) followed by task_next().
#
# Pass criteria:
#   1) "EXIT-RAN" appears on the serial mirror (the user task did
#      execute its main body).
#   2) the Testing!/Abc! demo keeps printing AFTER EXIT-RAN; the kernel
#      did not crash when reaping the exited process.

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log" 'EXIT-RAN' 'Abc!'

grep -q 'EXIT-RAN' "$log" || {
    echo "FAIL: 'EXIT-RAN' not in serial - EXIT task never ran main()"
    echo "      head 20 lines:"; head -20 "$log"
    exit 1
}

post=$(awk '/EXIT-RAN/{found=1; next} found' "$log")
if ! echo "$post" | grep -qE 'Abc!|Testing!'; then
    echo "FAIL: no userland prints after EXIT-RAN - kernel may have crashed in process_terminate"
    echo "      first 200 chars after EXIT-RAN: $(echo "$post" | head -c 200)"
    exit 1
fi

exit 0
