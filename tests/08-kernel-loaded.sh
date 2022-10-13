#!/bin/bash
# tests/08-kernel-loaded.sh
#
# Ch 32 - the boot sector loads 100 sectors starting at LBA 1 into
# 0x100000 via ATA PIO, then jumps to 0x100000. The kernel.asm stub
# reloads segment selectors and spins. After boot we expect EIP to be
# inside the 0x100000 region (specifically near the `jmp $` in kernel.asm).

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

# Sanity check the boot image.
boot_size=$(stat -c%s bin/boot.bin)
[ "$boot_size" = "512" ] || { echo "FAIL: boot.bin is $boot_size bytes"; exit 1; }

os_size=$(stat -c%s bin/os.bin)
# Layout: 512 (boot) + N (kernel.bin) + 100 sectors of zeros.
# Lower bound: 512 + at_least_one_kernel_byte + 100*512 = 51713.
[ "$os_size" -ge 51713 ] || { echo "FAIL: os.bin is $os_size bytes, expected at least 51713"; exit 1; }

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

eip=$(grep -oE 'EIP=[0-9a-fA-F]+' "$regs" | head -1 | cut -d= -f2)
if [ -z "$eip" ]; then
    echo "FAIL: could not parse EIP"
    sed -n '1,40p' "$regs"
    exit 1
fi

eip_dec=$((16#$eip))
# Kernel starts at 0x100000 (1 MiB = 1048576). The kernel.asm stub is small;
# EIP at boot end should land somewhere in [0x100000, 0x101000].
if [ "$eip_dec" -lt 1048576 ] || [ "$eip_dec" -gt 1052672 ]; then
    echo "FAIL: EIP=$eip ($eip_dec dec) not in expected kernel range [0x100000, 0x101000]"
    sed -n '1,40p' "$regs"
    exit 1
fi

exit 0
