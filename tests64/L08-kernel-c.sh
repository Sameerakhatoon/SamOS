#!/bin/bash
# tests64/L08-kernel-c.sh
#
# Lecture 8 - hand off from kernel.asm long_mode_entry into a
# 64-bit kernel_main written in C.
#
# Asserts via QEMU monitor `info registers`:
#   1) Long-mode invariants are still satisfied (Lecture 7 sanity):
#      CS = 0x0018, CR0.PG = 1, CR4.PAE = 1, EFER.LMA = 1.
#   2) RIP equals the address of kernel_main as resolved from
#      build/kernelfull.o (so we know the `jmp kernel_main` at
#      the end of long_mode_entry actually landed in C).
#
# kernel_main is currently a `while (1) {}` stub, so once the CPU
# enters it the RIP doesn't move - giving a stable snapshot
# target.

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

# Find kernel_main's linked address. The linker places the kernel
# image at 0x100000, and nm reports kernel_main relative to that
# base (offset from start of the relocatable). Sum them.
kmain_off=$(/home/samee/opt/cross/bin/x86_64-elf-nm build/kernelfull.o \
    | awk '$3 == "kernel_main" { print $1 }')
[ -n "$kmain_off" ] || { echo "FAIL: kernel_main symbol not found in kernelfull.o"; exit 1; }
kmain_addr=$(printf "%016x" $((0x100000 + 16#$kmain_off)))

regs=$(mktemp)
trap 'rm -f "$regs"' EXIT

( sleep 2; printf 'info registers\nquit\n' ) | timeout 12 qemu-system-x86_64 \
        -hda bin/os.bin -m 256 -accel tcg -display none -monitor stdio \
        -no-reboot > "$regs" 2>&1

rip=$(grep -oE 'RIP=[0-9a-f]+' "$regs" | head -1 | cut -d= -f2)
cs=$(grep -oE 'CS =[0-9a-f]+' "$regs" | head -1 | cut -d= -f2)
cr0=$(grep -oE 'CR0=[0-9a-f]+' "$regs" | head -1 | cut -d= -f2)
cr4=$(grep -oE 'CR4=[0-9a-f]+' "$regs" | head -1 | cut -d= -f2)
efer=$(grep -oE 'EFER=[0-9a-f]+' "$regs" | head -1 | cut -d= -f2)

ok=1

# Long-mode invariants (Lecture 7 still standing)
[ "$cs" = "0018" ] || { echo "FAIL: CS=$cs, expected 0018"; ok=0; }
top=$(printf '%d' "0x${cr0:0:1}"); [ $((top & 8)) -ne 0 ] || { echo "FAIL: CR0 PG"; ok=0; }
pae_byte=$(printf '%d' "0x${cr4:(-2):2}"); [ $((pae_byte & 0x20)) -ne 0 ] || { echo "FAIL: CR4 PAE"; ok=0; }
efer_dec=$((16#${efer##*([0])})); [ $((efer_dec & 0x400)) -ne 0 ] || { echo "FAIL: EFER LMA"; ok=0; }

# Hand-off into C: RIP == kernel_main
if [ "$rip" != "$kmain_addr" ]; then
    echo "FAIL: RIP=$rip, expected kernel_main at $kmain_addr"
    ok=0
fi

if [ $ok -ne 1 ]; then
    echo "--- register dump ---"
    grep -E 'CS =|RIP=|CR0=|CR4=|EFER' "$regs"
    exit 1
fi
exit 0
