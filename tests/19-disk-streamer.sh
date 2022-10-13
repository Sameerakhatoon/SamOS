#!/bin/bash
# tests/19-disk-streamer.sh
#
# Ch 60 - the disk streamer abstracts sector-grained PIO into byte-grained
# reads. kernel_main creates a stream, seeks to byte 0x1FE (offset 510
# inside sector 0, i.e. the boot signature), reads 2 bytes, and prints
# them as a 32-bit hex. Expected on-screen: "ss=000055AA".

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

if echo "$chars" | grep -q 'ss=000055AA'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'ss=000055AA'"
echo "      first 700 chars: $(echo "$chars" | head -c 700)"
exit 1
