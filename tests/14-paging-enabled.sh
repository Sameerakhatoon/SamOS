#!/bin/bash
# tests/14-paging-enabled.sh
#
# Ch 51 - the kernel sets CR0.PG=1 after building an identity-mapped
# 4 GiB page directory. Verify two things via QEMU monitor:
#   - CR0 bit 31 (PG) is set
#   - CR3 holds a non-zero value (the page-directory address kheap returned)

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

regs=$(mktemp)
cmd=$(mktemp)
trap 'rm -f "$regs" "$cmd"' EXIT

printf 'info registers\nquit\n' > "$cmd"

( sleep 7; cat "$cmd" ) | timeout 25 qemu-system-x86_64 \
        -hda bin/os.bin \
        -m 256 \
        -accel tcg \
        -display none \
        -monitor stdio \
        -no-reboot \
        > "$regs" 2>&1

cr0=$(grep -oE 'CR0=[0-9a-fA-F]+' "$regs" | head -1 | cut -d= -f2)
cr3=$(grep -oE 'CR3=[0-9a-fA-F]+' "$regs" | head -1 | cut -d= -f2)

if [ -z "$cr0" ] || [ -z "$cr3" ]; then
    echo "FAIL: could not parse CR0 or CR3"
    sed -n '1,40p' "$regs"
    exit 1
fi

# CR0 is 8 hex digits. PG is bit 31, so the top hex digit's high bit.
# E.g. CR0=80000011 means PG=1; CR0=00000011 means PG=0.
top_nibble=$(printf '%d' "0x${cr0:0:1}")
if [ $((top_nibble & 8)) -eq 0 ]; then
    echo "FAIL: CR0=$cr0 has PG bit clear; expected PG=1"
    sed -n '1,40p' "$regs"
    exit 1
fi

# CR3 should point at the page directory kzalloc returned. Any non-zero
# value within the heap pool is acceptable. The heap starts at 0x01000000.
cr3_dec=$((16#$cr3))
if [ "$cr3_dec" -lt 16777216 ]; then
    echo "FAIL: CR3=$cr3 (=$cr3_dec) below heap start 0x01000000"
    sed -n '1,40p' "$regs"
    exit 1
fi

exit 0
