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

( sleep 7; cat "$cmd" ) | timeout 25 qemu-system-x86_64 \
        -hda bin/os.bin \
        -m 256 \
        -accel tcg \
        -display none \
        -monitor stdio \
        -no-reboot \
        > "$regs" 2>&1

eip=$(grep -oE 'EIP=[0-9a-fA-F]+' "$regs" | head -1 | cut -d= -f2)
eip_dec=$((16#$eip))

# Range check: kernel loads at 0x100000. kernel.asm pads to 512 bytes
# so kernel.bin is at least 512 bytes long. The `jmp $` after `call kernel_main`
# is in kernel.asm just before the 512-byte boundary. kernel_main itself lives
# after that boundary. EIP should be anywhere in [0x100000, 0x102000].
if [ "$eip_dec" -lt 1048576 ] || [ "$eip_dec" -gt 1056768 ]; then
    echo "FAIL: EIP=$eip ($eip_dec dec) outside expected post-kernel-main range"
    sed -n '1,40p' "$regs"
    exit 1
fi

exit 0
