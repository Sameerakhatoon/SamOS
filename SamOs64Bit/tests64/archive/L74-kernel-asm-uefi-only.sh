#!/bin/bash
# tests64/L74-kernel-asm-uefi-only.sh
#
# Lecture 74 - kernel.asm fully UEFI-shaped.
#
# kernel.asm now starts with [BITS 64] and `_start: cli; jmp
# long_mode_entry`. The 32-bit -> 64-bit transition prologue is
# GONE - UEFI hands us long mode already, so the dance (PAE,
# CR3, EFER.LME, CR0.PG, far jmp to L=1 CS) duplicates what
# UEFI did.
#
# long_mode_entry now also does its own:
#   - mov cr3, PML4_Table  (replace UEFI's page tables)
#   - lgdt [gdt_descriptor] (replace UEFI's GDT)
#   - far retfq to slot 3   (CS swap to our 64-bit code seg)
#
# gdt_descriptor base: dd gdt -> dq gdt. In long mode the GDTR
# is { u16 limit; u64 base }; the old 32-bit `dd` lost the high
# half on real hardware with the GDT > 4 GiB.
#
# SamOs DEVIATION:
#   This BREAKS the BIOS boot path. boot.asm transitions to
#   32-bit protected mode and jumps to 0x100000. The first
#   instruction at 0x100000 is now `cli` (0xFA, same in both
#   modes) but the subsequent jmp lands in long_mode_entry,
#   which is REX-prefixed 64-bit code that decodes as garbage
#   in 32-bit mode. The kernel will not run under QEMU's BIOS
#   any more.
#
# CI was previously bash /tmp/verify_8s.sh; from L74 onward we
# can only source-check until either an EDK2 build environment
# is set up OR boot.asm grows the long-mode transition.

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

ASM=src/kernel.asm
[ -f "$ASM" ] || { echo "FAIL: $ASM missing"; exit 1; }

# Should be [BITS 64] from the top.
head -50 "$ASM" | grep -q '^\[BITS 64\]' \
    || { echo "FAIL: $ASM not [BITS 64]"; exit 1; }

# The simplified _start must just cli + jmp.
grep -B 0 -A 3 '^_start:' "$ASM" | grep -q 'cli' \
    || { echo "FAIL: _start does not begin with cli"; exit 1; }
grep -B 0 -A 3 '^_start:' "$ASM" | grep -q 'jmp long_mode_entry' \
    || { echo "FAIL: _start does not jmp long_mode_entry"; exit 1; }

# long_mode_entry must install our CR3 and GDT.
grep -A 30 '^long_mode_entry:' "$ASM" | head -30 | grep -q 'mov rax, PML4_Table' \
    || { echo "FAIL: long_mode_entry does not install CR3"; exit 1; }
grep -A 30 '^long_mode_entry:' "$ASM" | head -30 | grep -q 'lgdt' \
    || { echo "FAIL: long_mode_entry does not lgdt"; exit 1; }

# far retfq to switch CS to slot 3.
grep -q 'retfq' "$ASM" \
    || { echo "FAIL: no retfq in $ASM"; exit 1; }

# gdt_descriptor base widened.
grep -A 8 '^gdt_descriptor:' "$ASM" | grep -q '^[[:space:]]*dq gdt' \
    || { echo "FAIL: gdt_descriptor base is not dq"; exit 1; }

# 32-bit prologue must be gone.
if grep -q '^\[BITS 32\]' "$ASM"; then
    echo "FAIL: stale [BITS 32] line in $ASM"
    exit 1
fi

# kernel.bin built (links cleanly with the new entry shape).
[ -f bin/kernel.bin ] || { echo "FAIL: bin/kernel.bin missing"; exit 1; }
exit 0
