#!/bin/bash
# tests/12-keyboard-fires-repeatedly.sh
#
# Ch 112 - send THREE keypresses with small gaps. Pre-Ch 112 the int21h
# handler printed "Keyboard pressed!" once per keydown (G01 + G02). After
# the IDT macro refactor IRQ1 goes through the generic interrupt_handler
# which only EOIs the PIC, so no message is printed. This test now asserts
# the kernel survives a burst of keypresses without crashing - boot-time
# smoke probes must still be visible in VGA.

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

dump=$(mktemp)
trap 'rm -f "$dump"' EXIT

(
    sleep 10
    printf 'sendkey a\n'; sleep 0.3
    printf 'sendkey b\n'; sleep 0.3
    printf 'sendkey c\n'; sleep 0.3
    printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump"
) | timeout 25 qemu-system-x86_64 \
        -hda bin/os.bin \
        -m 256 \
        -accel tcg \
        -display none \
        -vga std \
        -monitor stdio \
        -no-reboot \
        > /dev/null 2>&1

chars=$(od -An -v -tx1 -w1 "$dump" \
            | awk 'NR%2==1 {printf "%s\n", $1}' \
            | xxd -r -p \
            | tr '\0' ' ')

if echo "$chars" | grep -q 'bootsig=000055AA'; then
    exit 0
fi

echo "FAIL: VGA buffer lost 'bootsig=000055AA' marker after 3 sendkeys - kernel may have crashed on IRQ1"
echo "      first 400 chars: $(echo "$chars" | head -c 400)"
exit 1
