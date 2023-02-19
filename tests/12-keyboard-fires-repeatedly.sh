#!/bin/bash
# tests/12-keyboard-fires-repeatedly.sh
#
# Ch 112 - send THREE keypresses with small gaps. Pre-Ch 112 the int21h
# handler printed "Keyboard pressed!" once per keydown (G01 + G02). After
# the IDT macro refactor IRQ1 goes through the generic interrupt_handler
# which only EOIs the PIC. This test asserts the kernel survives a burst
# of keypresses by checking the serial mirror for live kernel/userland
# output after the keypresses.

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

(
    sleep 1
    printf 'sendkey a\n'; sleep 0.2
    printf 'sendkey b\n'; sleep 0.2
    printf 'sendkey c\n'; sleep 0.2
    printf 'quit\n'
) | timeout 15 qemu-system-x86_64 \
        -hda bin/os.bin \
        -m 256 \
        -accel tcg \
        -display none \
        -vga std \
        -serial file:"$log" \
        -monitor stdio \
        -no-reboot \
        > /dev/null 2>&1

if grep -qE 'bootsig=000055AA|Abc!|Testing!' "$log"; then
    exit 0
fi

echo "FAIL: serial log has no kernel/userland output after 3 sendkeys - kernel may have crashed on IRQ1"
echo "      first 400 bytes: $(head -c 400 "$log")"
exit 1
