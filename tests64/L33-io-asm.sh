#!/bin/bash
# tests64/L33-io-asm.sh
#
# Lecture 33 - 64-bit IO primitives.
#
# src/io/io.asm is rewritten under the AMD64 SysV ABI (args in
# RDI/RSI instead of the 32-bit cdecl stack-args). New dword
# variants insdw / outdw round out the byte / word / dword set.
#
# Nothing calls the new io.asm symbols yet (the keyboard /
# disk / IDT code that will use them is later in the arc).
# This test just confirms io.asm builds, links, and doesn't
# break the existing boot.

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

dump=$(mktemp)
trap 'rm -f "$dump"' EXIT

( sleep 2; printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump" ) | timeout 15 \
    qemu-system-x86_64 -hda bin/os.bin -m 256 -accel tcg -display none -vga std \
        -monitor stdio -no-reboot > /dev/null 2>&1

chars=$(od -An -v -tx1 -w1 "$dump" \
            | awk 'NR%2==1 {print $1}' \
            | xxd -r -p \
            | tr '\0' ' ')

ok=1
for tok in 'Hello 64-bit!' 'e820 total:' 'ABC' 'multiheap ready'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
