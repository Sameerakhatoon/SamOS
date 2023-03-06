#!/bin/bash
# tests/50-caps-lock.sh
#
# Ch 116 / 149 - behavioural test for samos_getkeyblock (cmd 2 wrapped
# in a wait loop, Ch 116) AND the caps-lock case toggling in the PS/2
# classic keyboard driver (Ch 149).
#
# The kernel loads a "KEY" blank.elf instance that loops:
#   c = samos_getkeyblock(); putchar('K'); putchar(':'); putchar(c); ...
# Caps-lock state in src/keyboard/classic.c is GLOBAL (a field on the
# static classic_keyboard struct, not per-process), so any 'a' typed
# while caps_lock is on is upper-cased before being pushed into the
# current process's buffer - whichever task is current. We send a burst
# of 'a's, toggle caps_lock, send another burst, toggle off, send a
# third. The KEY task will be the current process for some fraction of
# those IRQs and will print K:a / K:A / K:a accordingly.
#
# Pass criteria:
#   1) at least one K:a survives in the serial log (lowercase from
#      the pre-caps and post-second-toggle bursts).
#   2) at least one K:A survives (uppercase from the after-caps burst,
#      proving caps_lock toggled the case of subsequent ASCII).

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

(
    # Wait longer than the absolute worst case for kernel boot under
    # suite load (15 process_load_switch calls + disk image init).
    sleep 8
    for i in $(seq 1 300); do printf 'sendkey a\n'; done
    sleep 1
    printf 'sendkey caps_lock\n'
    sleep 0.5
    for i in $(seq 1 300); do printf 'sendkey a\n'; done
    sleep 1
    printf 'sendkey caps_lock\n'
    sleep 0.5
    for i in $(seq 1 300); do printf 'sendkey a\n'; done
    sleep 3
    printf 'quit\n'
) | timeout 90 qemu-system-x86_64 \
        -hda bin/os.bin \
        -m 256 \
        -accel tcg \
        -display none \
        -vga std \
        -serial file:"$log" \
        -monitor stdio \
        -no-reboot \
        > /dev/null 2>&1

lower=$(grep -c 'K:a' "$log" || true)
upper=$(grep -c 'K:A' "$log" || true)

ok=1
if [ "$lower" -lt 1 ]; then
    echo "FAIL: no K:a in serial - the KEY task never saw a lowercase keypress (cmd 2 broken?)"
    ok=0
fi
if [ "$upper" -lt 1 ]; then
    echo "FAIL: no K:A in serial - caps_lock toggle did not upper-case subsequent keys"
    ok=0
fi

if [ $ok -ne 1 ]; then
    echo "      K:a count: $lower, K:A count: $upper"
    echo "      K: variants in log:"
    grep -oE 'K:.' "$log" | sort | uniq -c | head -10
    exit 1
fi

exit 0
