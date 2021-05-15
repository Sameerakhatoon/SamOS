#!/bin/bash
# tests/32-fat16-read.sh
#
# Ch 75/77 - the FAT16 driver's read + seek are wired into the VFS.
# kernel_main opens /hello.txt, fseeks 2 bytes forward (SEEK_SET=2),
# then freads 11 bytes. hello.txt is "Hello world!\n" so the post-seek
# read should give us "llo world!\n", printed as "hs=llo world!".

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

if echo "$chars" | grep -q 'hs=llo world!'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'hs=llo world!'"
echo "      first 1200 chars: $(echo "$chars" | head -c 1200)"
exit 1
