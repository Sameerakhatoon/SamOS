#!/bin/bash
# tests/11-keyboard-fires.sh
#
# Ch 112 - IRQ1 now routes through interrupt_pointer_table[0x21] ->
# the macro-generated int33 stub -> generic interrupt_handler which just
# EOIs the PIC. There is no longer a visible "Keyboard pressed!" message;
# real keyboard handling lands when Ch 113's callback table + the classic
# driver's IRQ1 callback are wired together. This test now asserts that
# IRQ1 does NOT crash the kernel - i.e. the kernel's boot-time smoke
# probes are still visible in VGA after a keypress.

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

dump=$(mktemp)
trap 'rm -f "$dump"' EXIT

(
    sleep 8
    printf 'sendkey a\n'; sleep 1
    printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump"
) \
        | timeout 25 qemu-system-x86_64 \
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

echo "FAIL: VGA buffer lost 'bootsig=000055AA' marker after sendkey - kernel may have crashed on IRQ1"
echo "      first 300 chars: $(echo "$chars" | head -c 300)"
exit 1
