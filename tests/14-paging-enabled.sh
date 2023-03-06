#!/bin/bash
# tests/14-paging-enabled.sh
#
# Ch 51 - kernel sets CR0.PG=1 after building an identity-mapped 4 GiB
# page directory. Two checks:
#   - CR0 bit 31 (PG) is set
#   - CR3 holds a non-zero value above the heap start (0x01000000)
# The kernel also prints "PAGING_ON" to serial right after enable_paging,
# so the marker check is a cheap shortcut to confirm we got past it.

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

regs=$(mktemp)
log=$(mktemp)
trap 'rm -f "$regs" "$log"' EXIT

run_kernel_inspect "$regs" "$log" 'entering userland' '' 'info registers'

grep -q 'PAGING_ON' "$log" || { echo "FAIL: kernel never printed PAGING_ON (enable_paging path broken?)"; exit 1; }

cr0=$(grep -oE 'CR0=[0-9a-fA-F]+' "$regs" | head -1 | cut -d= -f2)
cr3=$(grep -oE 'CR3=[0-9a-fA-F]+' "$regs" | head -1 | cut -d= -f2)

if [ -z "$cr0" ] || [ -z "$cr3" ]; then
    echo "FAIL: could not parse CR0 or CR3"
    sed -n '1,40p' "$regs"
    exit 1
fi

top_nibble=$(printf '%d' "0x${cr0:0:1}")
if [ $((top_nibble & 8)) -eq 0 ]; then
    echo "FAIL: CR0=$cr0 has PG bit clear; expected PG=1"
    sed -n '1,40p' "$regs"
    exit 1
fi

cr3_dec=$((16#$cr3))
if [ "$cr3_dec" -lt 16777216 ]; then
    echo "FAIL: CR3=$cr3 (=$cr3_dec) below heap start 0x01000000"
    sed -n '1,40p' "$regs"
    exit 1
fi

exit 0
