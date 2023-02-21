#!/bin/bash
# tests/08-kernel-loaded.sh
#
# Ch 32 - the boot sector loads the kernel image to 0x100000 and jumps.
# Synchronize on the "entering userland..." serial marker, then ask QEMU
# monitor for `info registers` and verify EIP is inside the kernel image
# (steady-state spin or any C function) or at the user program region.

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

boot_size=$(stat -c%s bin/boot.bin)
[ "$boot_size" = "512" ] || { echo "FAIL: boot.bin is $boot_size bytes"; exit 1; }

os_size=$(stat -c%s bin/os.bin)
[ "$os_size" -ge 51713 ] || { echo "FAIL: os.bin is $os_size bytes, expected at least 51713"; exit 1; }

regs=$(mktemp)
log=$(mktemp)
trap 'rm -f "$regs" "$log"' EXIT

run_kernel_inspect "$regs" "$log" 'entering userland' '' 'info registers'

eip=$(grep -oE 'EIP=[0-9a-fA-F]+' "$regs" | head -1 | cut -d= -f2)
if [ -z "$eip" ]; then
    echo "FAIL: could not parse EIP"
    sed -n '1,40p' "$regs"
    exit 1
fi

eip_dec=$((16#$eip))
in_kernel=0
in_user=0
[ "$eip_dec" -ge 1048576 ] && [ "$eip_dec" -le 2097152 ] && in_kernel=1
[ "$eip_dec" -ge 4194304 ] && [ "$eip_dec" -le 4210688 ] && in_user=1
if [ $in_kernel -eq 0 ] && [ $in_user -eq 0 ]; then
    echo "FAIL: EIP=$eip ($eip_dec dec) outside [0x100000,0x200000] U [0x400000,0x401000]"
    sed -n '1,40p' "$regs"
    exit 1
fi

exit 0
