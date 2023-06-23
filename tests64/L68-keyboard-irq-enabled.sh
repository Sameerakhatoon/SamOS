#!/bin/bash
# tests64/L68-keyboard-irq-enabled.sh
#
# Lecture 68 - classic_keyboard_init now calls IRQ_enable(
# IRQ_KEYBOARD). After this, the PIC delivers keyboard
# scancodes to our IDT.
#
# No automated way to simulate a keypress in QEMU monitor
# mode, so the test just confirms the kernel boots, the IRQ
# enable doesn't crash anything, and the user-enter milestone
# still holds.

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

dump=$(mktemp)
trap 'rm -f "$dump"' EXIT

( sleep 5; printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump" ) | timeout 12 \
    qemu-system-x86_64 -hda bin/os.bin -m 256 -accel tcg -display none -vga std \
        -monitor stdio -no-reboot > /dev/null 2>&1

chars=$(od -An -v -tx1 -w1 "$dump" \
            | awk 'NR%2==1 {print $1}' \
            | xxd -r -p \
            | tr '\0' ' ')

ok=1
for tok in 'tss ready' 'user enter'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
