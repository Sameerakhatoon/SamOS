#!/bin/bash
# tests/05-enters-protected-mode.sh
#
# Ch 22 - boot the image, then ask QEMU monitor for `info registers` and
# verify two things:
#   - CR0 bit 0 (PE) is set: we are in Protected Mode
#   - CS selector is 0x08: we successfully far-jumped into the kernel code
#     segment defined in our GDT
#
# Visible output (BIOS teletype) is gone since the bootloader spins in
# 32-bit mode after the switch. CPU state is the only thing we can observe.

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

boot_size=$(stat -c%s bin/boot.bin)
if [ "$boot_size" != "512" ]; then
    echo "FAIL: bin/boot.bin = $boot_size bytes, expected 512"
    exit 1
fi

sig=$(head -c 512 bin/os.bin | tail -c 2 | xxd -p)
if [ "$sig" != "55aa" ]; then
    echo "FAIL: boot signature = $sig, expected 55aa"
    exit 1
fi

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

# Look for the CR0 line. QEMU monitor prints something like:
#   CR0=00000011 CR2=00000000 ...
# We want bit 0 of CR0 to be 1 (PE).
if ! grep -qE 'CR0=[0-9a-fA-F]*[1-9a-fA-F]' "$regs"; then
    echo "FAIL: CR0 line not found in QEMU output"
    echo "      output:"; sed -n '1,40p' "$regs"
    exit 1
fi

cr0=$(grep -oE 'CR0=[0-9a-fA-F]+' "$regs" | head -1 | cut -d= -f2)
if [ -z "$cr0" ]; then
    echo "FAIL: could not parse CR0"
    exit 1
fi

# Test bit 0.
pe=$(printf '%d' "0x${cr0:(-1)}")
if [ $((pe & 1)) -eq 0 ]; then
    echo "FAIL: CR0=$cr0 has PE bit clear; expected PE=1"
    exit 1
fi

# CS should equal our CODE_SEG selector = offset of gdt_code in the GDT = 0x08.
cs=$(grep -oE 'CS *=[0-9a-fA-F ]+' "$regs" | head -1 | grep -oE '=[0-9a-fA-F]+' | tr -d '= ')
if [ -z "$cs" ]; then
    # Alternate format: ES =0000 CS =0008 ...
    cs=$(grep -oE '\bCS\b *=*[0-9a-fA-F]+' "$regs" | head -1 | grep -oE '[0-9a-fA-F]+' | tail -1)
fi

if [ -z "$cs" ]; then
    echo "FAIL: could not parse CS"
    echo "      output:"; sed -n '1,40p' "$regs"
    exit 1
fi

# CS should be one of:
#   0x08 - kernel code (boot reached kernel.asm, kernel hasn't dropped to ring 3)
#   0x1B - user code (0x18 | RPL=3, post-Ch 98 the kernel drops into ring 3
#          before our 12 s sample fires)
cs_dec=$((16#$cs))
if [ "$cs_dec" != "8" ] && [ "$cs_dec" != "27" ]; then
    echo "FAIL: CS=$cs, expected 0008 (kernel) or 001B (user code post-iret)"
    echo "      registers:"; sed -n '1,40p' "$regs"
    exit 1
fi

exit 0
