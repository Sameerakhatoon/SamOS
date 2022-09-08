#!/bin/bash
# tests/15-paging-remap.sh
#
# Ch 52 - paging_set remaps virtual 0x1000 to a heap-allocated page.
# kernel_main writes 'A','B' through the literal address 0x1000, then
# prints the same bytes through the original heap pointer. Both prints
# should yield "AB", so the VGA buffer should contain "remap:ABAB"
# somewhere.

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

dump=$(mktemp)
cmd=$(mktemp)
trap 'rm -f "$dump" "$cmd"' EXIT

printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump" > "$cmd"

( sleep 7; cat "$cmd" ) | timeout 25 qemu-system-x86_64 \
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

if echo "$chars" | grep -q 'remap:ABAB'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'remap:ABAB'"
echo "      first 600 chars: $(echo "$chars" | head -c 600)"
exit 1
