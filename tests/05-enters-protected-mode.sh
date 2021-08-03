#!/bin/bash
# tests/05-enters-protected-mode.sh
#
# Ch 22 - boot the image, then ask QEMU monitor for `info registers` and
# verify two things:
#   - CR0 bit 0 (PE) is set: we are in Protected Mode
#   - CS selector is 0x08 (kernel code) or 0x1B (user code post-iret).
# We synchronize on the serial "entering userland..." marker so the
# inspection happens at steady state, not at a wall-clock sleep.

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

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
log=$(mktemp)
trap 'rm -f "$regs" "$log"' EXIT

run_kernel_inspect "$regs" "$log" 'entering userland' '' 'info registers'

cr0=$(grep -oE 'CR0=[0-9a-fA-F]+' "$regs" | head -1 | cut -d= -f2)
if [ -z "$cr0" ]; then
    echo "FAIL: could not parse CR0"
    sed -n '1,40p' "$regs"
    exit 1
fi

pe=$(printf '%d' "0x${cr0:(-1)}")
if [ $((pe & 1)) -eq 0 ]; then
    echo "FAIL: CR0=$cr0 has PE bit clear; expected PE=1"
    exit 1
fi

cs=$(grep -oE 'CS *=[0-9a-fA-F ]+' "$regs" | head -1 | grep -oE '=[0-9a-fA-F]+' | tr -d '= ')
if [ -z "$cs" ]; then
    cs=$(grep -oE '\bCS\b *=*[0-9a-fA-F]+' "$regs" | head -1 | grep -oE '[0-9a-fA-F]+' | tail -1)
fi

if [ -z "$cs" ]; then
    echo "FAIL: could not parse CS"
    sed -n '1,40p' "$regs"
    exit 1
fi

cs_dec=$((16#$cs))
if [ "$cs_dec" != "8" ] && [ "$cs_dec" != "27" ]; then
    echo "FAIL: CS=$cs, expected 0008 (kernel) or 001B (user code post-iret)"
    sed -n '1,40p' "$regs"
    exit 1
fi

exit 0
