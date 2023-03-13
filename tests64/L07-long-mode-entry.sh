#!/bin/bash
# tests64/L07-long-mode-entry.sh
#
# Lecture 7 - rewrite kernel.asm to enter 64-bit long mode.
#
# After the bootloader hands us off at 0x100000 in 32-bit protected
# mode, kernel.asm installs a 64-bit-aware GDT, turns on PAE in CR4,
# loads CR3 with our PML4, sets IA32_EFER.LME, turns on CR0.PG, and
# far-jumps to the 64-bit code segment. The CPU should land in long
# mode at long_mode_entry and spin there.
#
# Asserts via QEMU monitor `info registers`:
#   CS  = 0x0018  (the 64-bit code segment selector)
#   CR0.PG = 1   (paging on, bit 31)
#   CR4.PAE = 1  (PAE on, bit 5)
#   EFER bit 10 = 1 (LMA - Long Mode Active)
#   RIP is wider than 32 bits (64-bit register format on info registers)

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

regs=$(mktemp)
trap 'rm -f "$regs"' EXIT

( sleep 1; printf 'info registers\nquit\n' ) | timeout 10 qemu-system-x86_64 \
        -hda bin/os.bin -m 256 -accel tcg -display none -monitor stdio \
        -no-reboot > "$regs" 2>&1

cs=$(grep -oE 'CS =[0-9a-f]+' "$regs" | head -1 | cut -d= -f2)
cr0=$(grep -oE 'CR0=[0-9a-f]+' "$regs" | head -1 | cut -d= -f2)
cr4=$(grep -oE 'CR4=[0-9a-f]+' "$regs" | head -1 | cut -d= -f2)
efer=$(grep -oE 'EFER=[0-9a-f]+' "$regs" | head -1 | cut -d= -f2)

ok=1
[ "$cs" = "0018" ] || { echo "FAIL: CS=$cs, expected 0018"; ok=0; }

# CR0 bit 31 (PG)
top=$(printf '%d' "0x${cr0:0:1}")
[ $((top & 8)) -ne 0 ] || { echo "FAIL: CR0=$cr0 PG bit clear"; ok=0; }

# CR4 bit 5 (PAE)
pae_byte=$(printf '%d' "0x${cr4:(-2):2}")
[ $((pae_byte & 0x20)) -ne 0 ] || { echo "FAIL: CR4=$cr4 PAE bit clear"; ok=0; }

# EFER bit 10 (LMA - Long Mode Active). EFER is 64-bit shown as hex.
efer_low=$(echo "$efer" | sed 's/^0*//')
[ -z "$efer_low" ] && efer_low=0
efer_dec=$((16#$efer_low))
[ $((efer_dec & 0x400)) -ne 0 ] || { echo "FAIL: EFER=$efer LMA bit clear"; ok=0; }

# Sanity: QEMU prints a "CS64" tag on the CS line when in 64-bit mode
grep -q 'CS64' "$regs" || { echo "FAIL: QEMU did not tag CS as CS64"; ok=0; }

if [ $ok -ne 1 ]; then
    echo "--- register dump ---"
    grep -E 'CS =|RIP=|CR0=|CR4=|EFER' "$regs"
    exit 1
fi

exit 0
