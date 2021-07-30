#!/bin/bash
# tests/52-system-command.sh
#
# Ch 145 - behavioural test for samos_system_run + cmd 7
# (SYSTEM_COMMAND7_INVOKE_SYSTEM_COMMAND), the shell's command launcher.
#
# kernel_main loads a blank.elf instance with argv[0]="SH". Its main
# prints "SH-CALL", calls samos_system_run("blank.elf SHRAN"), and
# prints "SH-RETURN" when the scheduler eventually rotates back.
#
# The cmd 7 handler:
#   1) reads the parsed command_argument list from the user stack,
#   2) loads "0:/blank.elf" via process_load_switch,
#   3) injects the parsed arguments so the new task's argv[0] becomes
#      "blank.elf" and argv[1] becomes "SHRAN",
#   4) task_switches into the new task.
#
# blank.c's branch ladder does NOT match "blank.elf", so the loaded
# task falls into the default `print(argv[0])` loop and emits the
# literal string "blank.elf" repeatedly on serial.
#
# Pass criteria:
#   1) "SH-CALL" appears (SH task ran).
#   2) "blank.elf" appears on serial (cmd 7 loaded + injected the new
#      process and it reached the print loop with argv intact).
#   3) "SH-RETURN" appears (the kernel did not crash inside cmd 7 and
#      SH's saved state survived the context switch).

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log" 'SH-RETURN' 'blank.elf'

grep -q 'SH-CALL' "$log" || {
    echo "FAIL: 'SH-CALL' not in serial - SH task never reached its main"
    head -30 "$log"
    exit 1
}

if ! grep -q 'blank.elf' "$log"; then
    echo "FAIL: 'blank.elf' not in serial - cmd 7 loaded process did not reach its print loop"
    head -30 "$log"
    exit 1
fi

grep -q 'SH-RETURN' "$log" || {
    echo "FAIL: 'SH-RETURN' not in serial - cmd 7 may have crashed or SH never resumed"
    head -30 "$log"
    exit 1
}

exit 0
