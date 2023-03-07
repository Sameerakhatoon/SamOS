#!/bin/bash
# tests/12-keyboard-fires-repeatedly.sh
#
# Ch 112 / 115 - real keyboard round-trip across THREE distinct keys.
# Asserts the IRQ1 path correctly dispatches and the KEY task echoes
# 'a', 'b', AND 'c' as K:a, K:b, K:c on serial. Validates that the
# classic_keyboard_scancode_to_char map produces all three letters,
# not just one accidentally.

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

(
    sleep 10
    for i in $(seq 1 400); do printf 'sendkey a\n'; done
    sleep 1
    for i in $(seq 1 400); do printf 'sendkey b\n'; done
    sleep 1
    for i in $(seq 1 400); do printf 'sendkey c\n'; done
    sleep 3
    printf 'quit\n'
) | timeout 90 qemu-system-x86_64 \
        -hda bin/os.bin \
        -m 256 \
        -accel tcg \
        -display none \
        -vga std \
        -serial file:"$log" \
        -monitor stdio \
        -no-reboot \
        > /dev/null 2>&1

a=$(grep -c 'K:a' "$log" || true)
b=$(grep -c 'K:b' "$log" || true)
c=$(grep -c 'K:c' "$log" || true)

ok=1
[ "$a" -ge 1 ] || { echo "FAIL: no 'K:a' - sendkey 'a' lost"; ok=0; }
[ "$b" -ge 1 ] || { echo "FAIL: no 'K:b' - sendkey 'b' lost"; ok=0; }
[ "$c" -ge 1 ] || { echo "FAIL: no 'K:c' - sendkey 'c' lost"; ok=0; }

if [ $ok -ne 1 ]; then
    echo "      counts: a=$a b=$b c=$c"
    echo "      first 400 bytes: $(head -c 400 "$log")"
    exit 1
fi

exit 0
