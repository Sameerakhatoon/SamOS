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
trap 'rm -f "$regs"' EXIT

# Ch 116: blank.bin now blocks on `call getkey` before reaching print/sum.
# Send a key, give it a beat to propagate through IRQ1 -> keyboard_pop, then
# sample registers.
(
    sleep 12
    printf 'sendkey a\n'
    sleep 1
    printf 'info registers\nquit\n'
) | timeout 30 qemu-system-x86_64 \
        -hda bin/os.bin \
        -m 256 \
        -accel tcg \
        -display none \
        -monitor stdio \
        -no-reboot \
        > "$regs" 2>&1 || true

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

# Ch 108: blank.bin runs print then sum then loops on jmp $. The exact
# loop offset shifts each time the program grows; just require EIP is
# somewhere in [0x00400000, 0x00400100] (well inside the binary).
eip_dec=$((16#$eip))
if [ "$eip_dec" -lt $((16#400000)) ] || [ "$eip_dec" -gt $((16#400100)) ]; then
    echo "FAIL: EIP=$eip not in [0x00400000, 0x00400100]"
    ok=0
fi

# Ch 117 dropped the sum syscall from blank.asm; the program now loops on
# getkey -> putchar and never invokes cmd 0 again. We no longer pin EAX
# to a specific value here - just the CPL/CS/EIP-in-binary assertions.

if [ $ok -ne 1 ]; then
    sed -n '1,40p' "$regs"
    exit 1
fi
exit 0
