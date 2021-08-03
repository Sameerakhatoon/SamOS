#!/bin/bash
# tests/45-exception-handler.sh
#
# Ch 147 - behavioural test for idt_handle_exception.
#
# kernel_main loads a blank.elf instance with argv[0]="CRASH". Its main
# prints "CRASH-RAN" then does *(int*)0 = 0x42, which triggers a #PF
# (vector 0x0E) from user mode. The fault should be routed through
# interrupt_handler -> idt_handle_exception -> process_terminate ->
# task_next, and the surviving Testing!/Abc! tasks must keep running.
#
# Pass criteria:
#   1) "CRASH-RAN" is in the serial mirror (the user task did reach the
#      null deref).
#   2) the Testing!/Abc! demo keeps printing AFTER CRASH-RAN; the kernel
#      did not triple-fault on the user-mode #PF.

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

# marker1: CRASH did print its marker (reached null deref)
# marker2: kernel kept printing after; idt_handle_exception survived.
run_kernel_capture "$log" 'CRASH-RAN' 'Abc!'

grep -q 'CRASH-RAN' "$log" || {
    echo "FAIL: 'CRASH-RAN' not in serial - CRASH task never reached its null deref"
    echo "      head 20 lines:"; head -20 "$log"
    exit 1
}

# Take just the slice of the log AFTER CRASH-RAN and confirm the
# kernel kept producing userland output. If idt_handle_exception
# triple-faulted, no further prints would appear.
post=$(awk '/CRASH-RAN/{found=1; next} found' "$log")
if ! echo "$post" | grep -qE 'Abc!|Testing!'; then
    echo "FAIL: no userland prints after CRASH-RAN - kernel may have crashed on the user #PF"
    echo "      first 200 chars after CRASH-RAN: $(echo "$post" | head -c 200)"
    exit 1
fi

exit 0
