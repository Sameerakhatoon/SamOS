#!/bin/bash
# tests/22-strcpy.sh
#
# Ch 64 - strcpy("hi") into a stack buffer must produce "hi\0".
# kernel_main prints "scp=hi" followed by strlen as hex; the expected
# line is "scp=hi00000002".

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

if echo "$chars" | grep -q 'scp=hi00000002'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'scp=hi00000002'"
echo "      first 700 chars: $(echo "$chars" | head -c 700)"
exit 1
