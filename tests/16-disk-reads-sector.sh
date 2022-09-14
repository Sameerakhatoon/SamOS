#!/bin/bash
# tests/16-disk-reads-sector.sh
#
# Ch 54 - disk_read_sector reads one 512-byte sector from LBA 0 (the
# boot sector) into a stack buffer. kernel_main prints buf[510..511]
# as a 32-bit hex (top 16 bits 0, bottom 16 bits = the two boot-signature
# bytes). For a valid PC boot sector buf[510]=0x55, buf[511]=0xAA, so
# we expect "bootsig=000055AA" on screen.

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

if echo "$chars" | grep -q 'bootsig=000055AA'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'bootsig=000055AA'"
echo "      first 600 chars: $(echo "$chars" | head -c 600)"
exit 1
