#!/bin/bash
# tests/17-string-utils.sh
#
# Ch 58 - kernel_main calls strnlen("hello", 100) and prints the result
# as 8 hex digits. The expected line is "slen=00000005". This verifies
# the new src/string/string.c implementation and that kernel.c can call
# into it correctly.

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

if echo "$chars" | grep -q 'slen=00000005'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'slen=00000005'"
echo "      first 600 chars: $(echo "$chars" | head -c 600)"
exit 1
