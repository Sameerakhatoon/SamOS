#!/bin/bash
# tests/02-bootloader-prints-hello.sh
#
# Verifies the Ch5/Ch6 bootloader:
#   - bin/boot.bin is 512 bytes ending in 0x55 0xAA
#   - bin/os.bin boots in QEMU and writes "Hello, World!" to VGA text RAM
#
# The bootloader uses BIOS INT 0x10 (teletype) which writes to VGA text
# memory at 0xB8000, not to the serial port. So we use the QEMU monitor's
# `pmemsave` to dump that region after a brief boot and scan for the message.
# VGA text mode = (char, attribute) pairs - strip attr bytes before grep.

set -e

cd "$(dirname "$0")/.."

# Build.
./build.sh > /dev/null

# 1. Binary shape.
size=$(stat -c%s bin/boot.bin)
if [ "$size" != "512" ]; then
    echo "FAIL: bin/boot.bin = $size bytes, expected 512"
    exit 1
fi

sig=$(tail -c 2 bin/boot.bin | xxd -p)
if [ "$sig" != "55aa" ]; then
    echo "FAIL: boot signature = $sig, expected 55aa"
    exit 1
fi

# 2. Boot in QEMU and dump VGA text RAM via the monitor.
dump=$(mktemp)
cmd=$(mktemp)
trap 'rm -f "$dump" "$cmd"' EXIT

printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump" > "$cmd"

# 2-second delay so SeaBIOS finishes posting + our bootloader runs.
( sleep 2; cat "$cmd" ) | timeout 8 qemu-system-x86_64 \
        -hda bin/os.bin \
        -m 16 \
        -accel tcg \
        -display none \
        -vga std \
        -monitor stdio \
        -no-reboot \
        > /dev/null 2>&1

if [ ! -s "$dump" ]; then
    echo "FAIL: VGA memory dump empty"
    exit 1
fi

# VGA text mode = 80x25 cells × 2 bytes (char, attribute). Pull out the char
# bytes only. Replace NUL with space so grep can find a phrase that spans
# rows (BIOS may add row padding between SeaBIOS banner and our output).
chars=$(od -An -v -tx1 -w1 "$dump" \
            | awk 'NR%2==1 {printf "%s\n", $1}' \
            | xxd -r -p \
            | tr '\0' ' ')

if echo "$chars" | grep -q "Hello, World!"; then
    exit 0
fi

echo "FAIL: VGA buffer didn't contain 'Hello, World!'"
echo "      first 200 chars: $(echo "$chars" | head -c 200)"
exit 1
