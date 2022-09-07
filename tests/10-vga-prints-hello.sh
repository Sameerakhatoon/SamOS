#!/bin/bash
# tests/10-vga-prints-hello.sh
#
# Ch 38 - kernel_main initializes the VGA terminal and prints
# "Hello world!\ntest". Verify both strings show up in VGA text RAM.
#
# Same VGA-buffer-grep approach as the early-real-mode tests.

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

# Pull char bytes out, replace NUL with space, scan for both strings.
chars=$(od -An -v -tx1 -w1 "$dump" \
            | awk 'NR%2==1 {printf "%s\n", $1}' \
            | xxd -r -p \
            | tr '\0' ' ')

ok=1
echo "$chars" | grep -q 'Hello world!' || { echo "FAIL: 'Hello world!' not in VGA buffer"; ok=0; }
echo "$chars" | grep -q 'test'         || { echo "FAIL: 'test' not in VGA buffer";          ok=0; }

if [ $ok -ne 1 ]; then
    echo "      first 300 chars: $(echo "$chars" | head -c 300)"
    exit 1
fi

exit 0
