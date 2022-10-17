#!/bin/bash
# tests/33-fstat.sh
#
# Ch 78+79 - VFS fstat dispatches to fat16_stat, which reads the
# size and attribute byte off the cached fat_directory_item. hello.txt
# is 13 bytes ("Hello world!\n") with no read-only attribute, so the
# kernel prints "fz=0000000D ffl=00000000".

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

ok=1
echo "$chars" | grep -q 'fz=0000000D'  || { echo "FAIL: filesize != 13"; ok=0; }
echo "$chars" | grep -q 'ffl=00000000' || { echo "FAIL: flags != 0"; ok=0; }

if [ $ok -ne 1 ]; then
    echo "      first 1200 chars: $(echo "$chars" | head -c 1200)"
    exit 1
fi
exit 0
