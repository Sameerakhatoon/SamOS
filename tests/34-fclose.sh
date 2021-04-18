#!/bin/bash
# tests/34-fclose.sh
#
# Ch 80+81 - fclose dispatches into fat16_close, which frees the
# fat_item chain + the fat_file_descriptor; then the VFS layer clears
# file_descriptors[fd-1] and kfrees the wrapping file_descriptor.
# kernel_main prints "afterclose=ok" after the call; if anything in
# the free chain double-frees or scrolls VGA, the marker doesn't make
# it onto the screen.

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

if echo "$chars" | grep -q 'afterclose=ok'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'afterclose=ok'"
echo "      first 1500 chars: $(echo "$chars" | head -c 1500)"
exit 1
