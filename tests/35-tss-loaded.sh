#!/bin/bash
# tests/35-tss-loaded.sh
#
# Ch 90 - the kernel installs the TSS as GDT offset 0x28 and runs ltr.
# Verify via QEMU monitor's `info registers` that the TR (Task Register)
# is loaded with selector 0x28.

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

regs=$(mktemp)
cmd=$(mktemp)
trap 'rm -f "$regs" "$cmd"' EXIT

printf 'info registers\nquit\n' > "$cmd"

( sleep 10; cat "$cmd" ) | timeout 25 qemu-system-x86_64 \
        -hda bin/os.bin \
        -m 256 \
        -accel tcg \
        -display none \
        -monitor stdio \
        -no-reboot \
        > "$regs" 2>&1

# TR appears as "TR =0028 ..." in QEMU's info registers output.
if grep -qE 'TR[[:space:]]*=[[:space:]]*0028' "$regs"; then
    exit 0
fi

echo "FAIL: TR not loaded with 0x28"
sed -n '1,60p' "$regs"
exit 1
