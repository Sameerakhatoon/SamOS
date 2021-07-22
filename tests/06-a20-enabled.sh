#!/bin/bash
# tests/06-a20-enabled.sh
#
# Ch 27 - verify the A20 line is enabled after boot. QEMU monitor's
# `info registers` reports the A20 state as "A20=1" or "A20=0". Look for
# that line and require A20=1.

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

regs=$(mktemp)
cmd=$(mktemp)
trap 'rm -f "$regs" "$cmd"' EXIT

printf 'info registers\nquit\n' > "$cmd"

( sleep 0.4; cat "$cmd" ) | timeout 25 qemu-system-x86_64 \
        -hda bin/os.bin \
        -m 256 \
        -accel tcg \
        -display none \
        -monitor stdio \
        -no-reboot \
        > "$regs" 2>&1

if grep -q "A20=1" "$regs"; then
    exit 0
fi

if grep -q "A20=0" "$regs"; then
    echo "FAIL: A20 line is disabled after boot"
    sed -n '1,30p' "$regs"
    exit 1
fi

echo "FAIL: could not find A20 status in QEMU output"
sed -n '1,30p' "$regs"
exit 1
