#!/bin/bash
# tests/23-fat16-registers.sh
#
# Ch 65 - the FAT16 driver stub registers itself with the VFS via
# fs_static_load -> fs_insert_filesystem(fat16_init()). When
# disk_search_and_init calls fs_resolve(&disk), it walks the table,
# calls fat16_resolve (which currently returns 0 for any disk), and
# stores fat16_fs in disk.filesystem. kernel_main prints the name.
# Expected on-screen: "fs=FAT16".

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

if echo "$chars" | grep -q 'fs=FAT16'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'fs=FAT16'"
echo "      first 700 chars: $(echo "$chars" | head -c 700)"
exit 1
