#!/bin/bash
# tests/12-keyboard-fires-repeatedly.sh
#
# G01 regression sentinel - send THREE keypresses with small gaps. The kernel
# should print "Keyboard pressed!" THREE times (counted via grep). Pre-G01 the
# handler did not drain port 0x60 so only the first key produced output.

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

dump=$(mktemp)
trap 'rm -f "$dump"' EXIT

(
    sleep 2.5
    printf 'sendkey a\n'; sleep 0.3
    printf 'sendkey b\n'; sleep 0.3
    printf 'sendkey c\n'; sleep 0.3
    printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump"
) | timeout 12 qemu-system-x86_64 \
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

count=$(echo "$chars" | grep -o 'Keyboard pressed!' | wc -l)

# G02 filters key-release scancodes so each sendkey produces exactly one
# print. 3 keys -> exactly 3 lines.
if [ "$count" = "3" ]; then
    exit 0
fi

echo "FAIL: expected exactly 3 'Keyboard pressed!' messages, got $count"
echo "      first 400 chars: $(echo "$chars" | head -c 400)"
exit 1
