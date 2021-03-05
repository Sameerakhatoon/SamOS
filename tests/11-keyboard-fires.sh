#!/bin/bash
# tests/11-keyboard-fires.sh
#
# Ch 45 - the kernel's int21h_handler prints "Keyboard pressed!" each time
# IRQ 1 fires. Boot, wait for the kernel to settle, send ONE key via QEMU
# monitor, wait a moment, dump VGA RAM, look for the message.
#
# Only one key is sent because IRQ1 keeps re-firing on subsequent presses
# until we drain port 0x60 (real keyboard handling comes in a later chapter).

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

dump=$(mktemp)
trap 'rm -f "$dump"' EXIT

( sleep 2.5; printf 'sendkey a\n'; sleep 0.5; \
  printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump" ) \
        | timeout 10 qemu-system-x86_64 \
        -hda bin/os.bin \
        -m 16 \
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

if echo "$chars" | grep -q 'Keyboard pressed!'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'Keyboard pressed!' after sendkey"
echo "      first 300 chars: $(echo "$chars" | head -c 300)"
exit 1
