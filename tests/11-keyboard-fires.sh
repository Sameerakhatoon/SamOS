#!/bin/bash
# tests/11-keyboard-fires.sh
#
# Ch 112 / 115 - real keyboard round-trip. The kernel's IRQ1 path
# (vector 0x21 -> generic interrupt_handler -> classic_keyboard_handle_
# interrupt -> keyboard_push) must deliver a sendkey 'a' to the
# current process's buffer, and the KEY task's getkeyblock+putchar
# loop must echo it as "K:a" on serial. So a passing test 11 means:
#   1) IRQ1 fired without crashing the kernel,
#   2) keyboard_push wrote 'a' into the KEY task's buffer (per G11),
#   3) cmd 2 (getkey/getkeyblock) returned that 'a',
#   4) cmd 3 putchar emitted the four bytes K, :, a, \n to serial.

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

(
    # Wait past kernel-init + the multi-task boot loads. Burst enough
    # 'a' keys that KEY's ~1/15 share of the rotation yields at least
    # one delivered keystroke even under heavy suite load.
    sleep 10
    for i in $(seq 1 400); do printf 'sendkey a\n'; done
    sleep 3
    printf 'quit\n'
) | timeout 60 qemu-system-x86_64 \
        -hda bin/os.bin \
        -m 256 \
        -accel tcg \
        -display none \
        -vga std \
        -serial file:"$log" \
        -monitor stdio \
        -no-reboot \
        > /dev/null 2>&1

# Round-trip assertion: at least one K:a must appear on serial.
n=$(grep -c 'K:a' "$log" || true)
if [ "$n" -ge 1 ]; then
    exit 0
fi

echo "FAIL: no 'K:a' on serial - sendkey 'a' did not round-trip through IRQ1, cmd 2, cmd 3"
echo "      first 300 bytes: $(head -c 300 "$log")"
exit 1
