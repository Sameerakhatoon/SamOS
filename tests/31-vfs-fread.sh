#!/bin/bash
# tests/31-vfs-fread.sh
#
# Ch 73 - fread is wired in the VFS layer. With fd=0 (which is always
# invalid because descriptors start at 1) fread short-circuits to
# -EINVARG. -EINVARG is -2; cast to unsigned that is FFFFFFFE.
# kernel_main prints "frd=FFFFFFFE".

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

if echo "$chars" | grep -q 'frd=FFFFFFFE'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'frd=FFFFFFFE'"
echo "      first 1000 chars: $(echo "$chars" | head -c 1000)"
exit 1
