#!/bin/bash
# tests/03-divzero-handler-fires.sh
#
# Ch 13 - the bootloader installs an ISR at vector 0 and then deliberately
# does `div ax` with ax = 0. The CPU raises #DE which jumps to our handler,
# and the handler prints "Divide by zero error!" via BIOS teletype.
#
# Same VGA-grep approach as test 02.

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

dump=$(mktemp)
cmd=$(mktemp)
trap 'rm -f "$dump" "$cmd"' EXIT

printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump" > "$cmd"

( sleep 2; cat "$cmd" ) | timeout 8 qemu-system-x86_64 \
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

if echo "$chars" | grep -q "Divide by zero error!"; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain the divide-by-zero message"
echo "      first 300 chars: $(echo "$chars" | head -c 300)"
exit 1
