#!/bin/bash
# tests/25-disk-id-and-fs-private.sh
#
# Ch 67 - disk_search_and_init now zero-initialises disk.id and leaves
# disk.fs_private null (FAT16's resolver will fill it in later).
# kernel_main prints both as hex; expect "did=00000000 priv=00000000".

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

if echo "$chars" | grep -q 'did=00000000'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'did=00000000'"
echo "      first 800 chars: $(echo "$chars" | head -c 800)"
exit 1
