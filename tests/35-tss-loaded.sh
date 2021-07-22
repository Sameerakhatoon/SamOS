#!/bin/bash
# tests/35-tss-loaded.sh
#
# Ch 90 - the kernel installs the TSS as GDT offset 0x28 and runs ltr.
# Two checks:
#   - the kernel printed "TSS_OK" to serial right after tss_load
#   - QEMU monitor's `info registers` shows TR=0028 at steady state

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

regs=$(mktemp)
log=$(mktemp)
trap 'rm -f "$regs" "$log"' EXIT

run_kernel_inspect "$regs" "$log" 'info registers'

grep -q 'TSS_OK' "$log" || { echo "FAIL: kernel never printed TSS_OK (tss_load path broken?)"; exit 1; }

if grep -qE 'TR[[:space:]]*=[[:space:]]*0028' "$regs"; then
    exit 0
fi

echo "FAIL: TR not loaded with 0x28"
sed -n '1,60p' "$regs"
exit 1
