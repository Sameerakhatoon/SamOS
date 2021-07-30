#!/bin/bash
# tests/51-process-load-start.sh
#
# Ch 138 - behavioural test for samos_process_load_start (cmd 6
# SYSTEM_COMMAND6_PROCESS_LOAD_START).
#
# kernel_main loads a blank.elf instance with argv[0]="LD". Its main
# prints "LD-CALL", invokes samos_process_load_start("0:/blank.elf"),
# then prints "LD-RETURN" once it gets rescheduled (cmd 6's handler
# task_switches into the newly loaded process and iretds, so the LD
# caller does not get an immediate return - the post-call print only
# fires when the scheduler eventually rotates back).
#
# Pass criteria:
#   1) "LD-CALL" appears (LD reached its main).
#   2) "LD-RETURN" appears (the kernel did not crash inside cmd 6,
#      LD's saved state survived the context switch, and the scheduler
#      rotated back to LD).

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log" 'LD-RETURN' 'Abc!'

grep -q 'LD-CALL' "$log" || {
    echo "FAIL: 'LD-CALL' not in serial - LD task never reached its main"
    head -30 "$log"
    exit 1
}

grep -q 'LD-RETURN' "$log" || {
    echo "FAIL: 'LD-RETURN' not in serial - cmd 6 either crashed or LD never resumed"
    head -30 "$log"
    exit 1
}

exit 0
