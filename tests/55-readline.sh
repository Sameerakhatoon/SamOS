#!/bin/bash
# tests/55-readline.sh
#
# Ch 136 - behavioural test for samos_terminal_readline.
#
# kernel_main loads a blank.elf instance with argv[0]="RL". Its main
# prints "RL-START\n", then calls samos_terminal_readline(buf, 7, true)
# which loops samos_getkeyblock (cmd 2) until either '\n' arrives or
# the 7-char buffer fills. With output_while_typing=true each accepted
# char is putchar'd back so the chars that DID land in the RL task's
# buffer are observable on the serial mirror.
#
# Because keyboard_push delivers to whichever process is current at
# IRQ1 time (G11 keeps current_process in lockstep with the scheduled
# task), we send LOTS of 'x' and Enter keys to maximize the chance
# that RL gets enough chars and at least one Enter while it is the
# current task.
#
# Pass criteria:
#   1) "RL-START" appears (the RL task did reach its main).
#   2) "RL-DONE:" appears, meaning samos_terminal_readline returned
#      (either '\n' was read or the buf filled).

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

(
    sleep 5
    for i in $(seq 1 200); do printf 'sendkey x\n'; done
    sleep 2
    for i in $(seq 1 200); do printf 'sendkey ret\n'; done
    sleep 3
    printf 'quit\n'
) | timeout 60 qemu-system-x86_64 \
        -hda bin/os.bin \
        -m 256 \
        -accel tcg \
        -display none \
        -vga std \
        -serial file:"$log" \
        -monitor stdio \
        -no-reboot \
        > /dev/null 2>&1

grep -q 'RL-START' "$log" || {
    echo "FAIL: 'RL-START' not in serial - RL task never ran"
    head -30 "$log"
    exit 1
}

grep -q 'RL-DONE' "$log" || {
    echo "FAIL: 'RL-DONE' not in serial - samos_terminal_readline never returned (cmd 2 getkey loop broken or no Enter ever landed in RL's buffer)"
    head -30 "$log"
    exit 1
}

exit 0
