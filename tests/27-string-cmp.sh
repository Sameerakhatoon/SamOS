#!/bin/bash
# tests/27-string-cmp.sh
#
# Ch 69 - tolower, strncmp, istrncmp, strnlen_terminator must all work.
# kernel_main prints three probes:
#   istr=00000001  -> istrncmp("AbC","abc",3) == 0 (case insensitive)
#   scmp=00000001  -> strncmp("abc","abd",3) != 0 (last char differs)
#   sterm=00000003 -> strnlen_terminator("foo bar",100,' ') == 3

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
echo "$chars" | grep -q 'istr=00000001'  || { echo "FAIL: istrncmp"; ok=0; }
echo "$chars" | grep -q 'scmp=00000001'  || { echo "FAIL: strncmp"; ok=0; }
echo "$chars" | grep -q 'sterm=00000003' || { echo "FAIL: strnlen_terminator"; ok=0; }

if [ $ok -ne 1 ]; then
    echo "      first 900 chars: $(echo "$chars" | head -c 900)"
    exit 1
fi
exit 0
