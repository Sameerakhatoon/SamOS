#!/bin/bash
# tests/30-fat16-open-miss.sh
#
# Ch 72 - fat16_open now walks the parsed root directory and returns
# ERROR(-EIO) if the requested file isn't present. The VFS fopen then
# coerces that to 0. The 16 MiB FAT volume is zero-filled (no files
# mcopied in yet), so fopen("0:/hello.txt", "r") -> 0. Expected
# on-screen "fop_miss=00000000".

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

if echo "$chars" | grep -q 'fop_miss=00000000'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'fop_miss=00000000'"
echo "      first 1000 chars: $(echo "$chars" | head -c 1000)"
exit 1
