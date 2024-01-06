#!/bin/bash
# tests64/L09-hello-terminal.sh
#
# Lecture 9 - restore terminal helpers and have kernel_main paint
# a banner. Asserts "Hello 64-bit!" lands in the VGA buffer at
# 0xB8000 after boot.

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

dump=$(mktemp)
trap 'rm -f "$dump"' EXIT

( sleep 2; printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump" ) | timeout 12 \
    qemu-system-x86_64 -hda bin/os.bin -m 256 -accel tcg -display none -vga std \
        -monitor stdio -no-reboot > /dev/null 2>&1

chars=$(od -An -v -tx1 -w1 "$dump" \
            | awk 'NR%2==1 {print $1}' \
            | xxd -r -p \
            | tr '\0' ' ')

if echo "$chars" | grep -q 'Hello 64-bit!'; then
    exit 0
fi

echo "FAIL: 'Hello 64-bit!' not in VGA buffer"
echo "      first 200 chars: $(echo "$chars" | head -c 200)"
exit 1
