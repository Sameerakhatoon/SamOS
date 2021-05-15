#!/bin/bash
# tests/09-kernel-main-runs.sh
#
# Ch 36 - the kernel.asm stub now `call kernel_main` (a C function) before
# spinning. Verify two things:
#   - kernel_main lives somewhere in the loaded kernel image (linker keeps it).
#   - After boot, EIP is past the `call` instruction. This means the call
#     either returned (kernel_main was empty so it returned) or we are inside
#     it. Either way, control reached kernel_main's call site.

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

# Symbol check: kernel_main must be a defined text symbol in kernelfull.o.
if ! "$HOME/opt/cross/bin/i686-elf-nm" build/kernelfull.o | grep -qE '^[0-9a-fA-F]+ T kernel_main$'; then
    echo "FAIL: kernel_main not found as a text symbol in build/kernelfull.o"
    "$HOME/opt/cross/bin/i686-elf-nm" build/kernelfull.o
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

eip=$(grep -oE 'EIP=[0-9a-fA-F]+' "$regs" | head -1 | cut -d= -f2)
eip_dec=$((16#$eip))

# Range check: EIP should be either:
#   inside the kernel image [0x100000, 0x102000] (boot reached kernel.asm /
#   kernel_main but didn't drop to ring 3 yet), OR
#   at the user program address 0x400000 (post-Ch 98 ring-3 drop).
in_kernel=0
in_user=0
[ "$eip_dec" -ge 1048576 ] && [ "$eip_dec" -le 1056768 ] && in_kernel=1
[ "$eip_dec" -ge 4194304 ] && [ "$eip_dec" -le 4198400 ] && in_user=1
if [ $in_kernel -eq 0 ] && [ $in_user -eq 0 ]; then
    echo "FAIL: EIP=$eip ($eip_dec dec) outside [0x100000,0x102000] U [0x400000,0x401000]"
    sed -n '1,40p' "$regs"
    exit 1
fi

exit 0
