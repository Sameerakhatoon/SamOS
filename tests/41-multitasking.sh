#!/bin/bash
# tests/41-multitasking.sh
#
# Ch 150 grand-finale sentinel - validates the whole post-Ch-100 chain
# end-to-end: ELF loader (Ch 121-126), user-mode syscalls, c_start,
# process_inject_arguments, PIT IRQ -> idt_clock -> task_next preempt,
# and the G05/G07/G08/G09/G10 guards.
#
# Mechanism: kernel_main loads blank.elf instances tagged "Testing!"
# and "Abc!" (plus the transient CRASH/EXIT/BS/MF/PC ones, which die
# fast). The serial mirror accumulates everything the print() syscall
# emits, so a single capture across the steady-state Testing!/Abc!
# rotation will contain BOTH strings if the scheduler is alive.

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

# Wait for at least one of each printed by userland tasks.
run_kernel_capture "$log" 'Testing!' 'Abc!'

testing=$(grep -o 'Testing' "$log" | wc -l)
abc=$(grep -o 'Abc'     "$log" | wc -l)

ok=1
if [ "$testing" -lt 5 ]; then
    echo "FAIL: only $testing 'Testing!' tokens on serial - task 1 (argv[0]=Testing!) may not be running"
    ok=0
fi
if [ "$abc" -lt 5 ]; then
    echo "FAIL: only $abc 'Abc' tokens on serial - task 2 (argv[0]=Abc!) not getting scheduled"
    ok=0
fi

if [ $ok -ne 1 ]; then
    echo "      serial log first 600 chars:"
    head -c 600 "$log"
    exit 1
fi
exit 0
