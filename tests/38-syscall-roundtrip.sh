#!/bin/bash
# tests/38-syscall-roundtrip.sh
#
# Ch 103+104 - user code in blank.bin now executes `mov eax,0; int 0x80`
# before its infinite jmp. That fires our isr80h_wrapper, the C handler
# dispatches to command 0 (sum stub), the wrapper iretds back to ring 3
# with EAX = 0. Then blank.bin loops at `jmp $`.
#
# Verify via QEMU monitor that the CPU is back in ring 3 (CPL=3, CS=0x1B)
# and EIP is past the `int 0x80` instruction (>= 0x00400007 = 5 bytes
# `mov eax,0` + 2 bytes `int 0x80`). If the syscall doesn't return, EIP
# would either stay inside the int instruction or end up in some kernel
# handler.

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

regs=$(mktemp)
cmd=$(mktemp)
trap 'rm -f "$regs" "$cmd"' EXIT

printf 'info registers\nquit\n' > "$cmd"

( sleep 12; cat "$cmd" ) | timeout 30 qemu-system-x86_64 \
        -hda bin/os.bin \
        -m 256 \
        -accel tcg \
        -display none \
        -monitor stdio \
        -no-reboot \
        > "$regs" 2>&1

eip=$(grep -oE 'EIP=[0-9a-fA-F]+' "$regs" | head -1 | cut -d= -f2)
cs=$(grep -oE 'CS =[0-9a-fA-F]+' "$regs" | head -1 | cut -d= -f2 | tr -d ' ')
cpl=$(grep -oE 'CPL=[0-9]' "$regs" | head -1 | cut -d= -f2)
eax=$(grep -oE 'EAX=[0-9a-fA-F]+' "$regs" | head -1 | cut -d= -f2)

ok=1
[ "$cpl" = "3" ]              || { echo "FAIL: CPL=$cpl (expected 3)"; ok=0; }
case "$cs" in
    001b|001B) ;;
    *) echo "FAIL: CS=$cs (expected 001B)"; ok=0; ;;
esac

# Ch 107: blank.bin is now
#   push 20  (2) push 30  (2) mov eax,0  (5) int 0x80  (2) add esp,8 (3) jmp $ (2)
# so the trailing `jmp $` lives at offset 0x0E (0x4000_000E). After
# the syscall returns the CPU runs add esp,8 then loops at 0x4000_000E.
eip_dec=$((16#$eip))
if [ "$eip_dec" -lt $((16#40000B)) ] || [ "$eip_dec" -gt $((16#400010)) ]; then
    echo "FAIL: EIP=$eip not in [0x0040000B, 0x00400010]"
    ok=0
fi

# Sum command returns 20 + 30 = 50 = 0x32; wrapper plumbs that into EAX.
case "$eax" in
    00000032) ;;
    *) echo "FAIL: EAX=$eax (expected 00000032, sum(20,30))"; ok=0; ;;
esac

if [ $ok -ne 1 ]; then
    sed -n '1,40p' "$regs"
    exit 1
fi
exit 0
