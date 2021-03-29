#!/bin/bash
# tests/18-path-parser.sh
#
# Ch 59 - pathparser_parse("0:/bin/shell.exe", NULL) must produce a
# path_root with drive_no=0, first->part="bin", first->next->part=
# "shell.exe". kernel_main prints these so we can grep the VGA buffer.

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

ok=1
echo "$chars" | grep -q 'pp_drv=00000000'    || { echo "FAIL: pp_drv != 0"; ok=0; }
echo "$chars" | grep -q 'pp_p1=bin'          || { echo "FAIL: pp_p1 != 'bin'"; ok=0; }
echo "$chars" | grep -q 'pp_p2=shell.exe'    || { echo "FAIL: pp_p2 != 'shell.exe'"; ok=0; }

if [ $ok -ne 1 ]; then
    echo "      first 700 chars: $(echo "$chars" | head -c 700)"
    exit 1
fi

exit 0
