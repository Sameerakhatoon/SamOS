#!/bin/bash
# tests/09-kernel-main-runs.sh
#
# Ch 36 - kernel.asm now calls C kernel_main(). Verify two things:
#   - kernel_main is linked in (symbol present).
#   - EIP at steady state is inside the kernel image or at the user
#     program address - i.e. control reached kernel_main without
#     triple-faulting.

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

if ! "$HOME/opt/cross/bin/i686-elf-nm" build/kernelfull.o | grep -qE '^[0-9a-fA-F]+ T kernel_main$'; then
    echo "FAIL: kernel_main not found as a text symbol in build/kernelfull.o"
    "$HOME/opt/cross/bin/i686-elf-nm" build/kernelfull.o
    exit 1
fi

regs=$(mktemp)
log=$(mktemp)
trap 'rm -f "$regs" "$log"' EXIT

run_kernel_inspect "$regs" "$log" 'entering userland' '' 'info registers'

eip=$(grep -oE 'EIP=[0-9a-fA-F]+' "$regs" | head -1 | cut -d= -f2)
eip_dec=$((16#$eip))

in_kernel=0
in_user=0
[ "$eip_dec" -ge 1048576 ] && [ "$eip_dec" -le 2097152 ] && in_kernel=1
# Accept any EIP in the user program region [0x400000, 0x500000)
# since the snapshot can land anywhere in blank.elf/shell.elf code.
[ "$eip_dec" -ge 4194304 ] && [ "$eip_dec" -lt 5242880 ] && in_user=1
if [ $in_kernel -eq 0 ] && [ $in_user -eq 0 ]; then
    echo "FAIL: EIP=$eip ($eip_dec dec) outside [0x100000,0x200000] U [0x400000,0x500000)"
    sed -n '1,40p' "$regs"
    exit 1
fi

exit 0
