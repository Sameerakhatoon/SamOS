#!/bin/bash
# tests/04-bootloader-reads-sector2.sh
#
# Ch 15 - boot sector reads sector 2 via BIOS INT 0x13 into 0x7E00 and prints
# the NUL-terminated string at the start. Sector 2 holds "Hello World!!!".
#
# Verifies:
#   - bin/os.bin is exactly 1024 bytes (boot.bin + data.bin)
#   - boot.bin (first 512) ends with 0x55 0xAA
#   - data.bin (second 512) contains the literal "Hello World!!!"
#   - QEMU boot shows that string on VGA text RAM

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

size=$(stat -c%s bin/os.bin)
if [ "$size" != "1024" ]; then
    echo "FAIL: bin/os.bin is $size bytes, expected 1024"
    exit 1
fi

sig=$(head -c 512 bin/os.bin | tail -c 2 | xxd -p)
if [ "$sig" != "55aa" ]; then
    echo "FAIL: boot signature is $sig, expected 55aa"
    exit 1
fi

if ! grep -aq 'Hello World!!!' bin/data.bin; then
    echo "FAIL: data.bin does not contain 'Hello World!!!'"
    exit 1
fi

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

if echo "$chars" | grep -q 'Hello World!!!'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'Hello World!!!'"
echo "      first 300 chars: $(echo "$chars" | head -c 300)"
exit 1
