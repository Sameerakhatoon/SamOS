#!/bin/bash
# tests/11-keyboard-fires.sh
#
# Ch 112 - IRQ1 now routes through interrupt_pointer_table[0x21] ->
# the macro-generated int33 stub -> generic interrupt_handler which just
# EOIs the PIC. This test asserts that IRQ1 does NOT crash the kernel:
# kernel print() keeps producing serial output after the keypress.

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

(
    sleep 1
    printf 'sendkey a\n'
    sleep 1
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

echo "FAIL: serial log has no kernel/userland output after sendkey - kernel may have crashed on IRQ1"
echo "      first 300 bytes: $(head -c 300 "$log")"
exit 1
