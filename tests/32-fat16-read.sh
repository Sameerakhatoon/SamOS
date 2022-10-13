#!/bin/bash
# tests/32-fat16-read.sh
#
# Ch 75 - the FAT16 driver's read function is wired into the VFS, and
# the Makefile now mformats the volume + mcopies hello.txt into it.
# kernel_main opens /hello.txt, freads 13 bytes, and prints them.
# The file content is "Hello world!\n", so the VGA buffer should
# contain "h=Hello world!".

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

dump=$(mktemp)
cmd=$(mktemp)
trap 'rm -f "$dump" "$cmd"' EXIT

printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump" > "$cmd"

( sleep 10; cat "$cmd" ) | timeout 25 qemu-system-x86_64 \
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

if echo "$chars" | grep -q 'h=Hello world!'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'h=Hello world!'"
echo "      first 1200 chars: $(echo "$chars" | head -c 1200)"
exit 1
