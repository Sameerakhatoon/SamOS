#!/bin/bash
# tests/24-fat16-struct-sizes.sh
#
# Ch 66 - the FAT16 driver declares the packed C structs that mirror
# on-disk layouts. fat16_check_sizes() returns 1 iff:
#   sizeof(fat_header)          == 36
#   sizeof(fat_header_extended) == 26
#   sizeof(fat_directory_item)  == 32
# kernel_main prints the result; expected on-screen "fszs=00000001".

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

if echo "$chars" | grep -q 'fszs=00000001'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'fszs=00000001'"
echo "      first 700 chars: $(echo "$chars" | head -c 700)"
exit 1
