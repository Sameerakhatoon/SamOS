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
    sleep 3
    printf 'info registers\nquit\n'
) | timeout 40 qemu-system-x86_64 \
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
eip_dec=$((16#$eip))

# Ch 117 turned blank.bin into a getkey -> putchar loop. At any sampled
# moment we may catch the CPU in ring 3 (inside the user binary) OR in
# ring 0 (servicing IRQ1 / int 0x80 cmd 2 or 3). Both indicate the system
# is live and processing the keypress. Accept either:
#   (a) CPL=3, CS=001B, EIP in [0x00400000, 0x00400100] (user binary), OR
#   (b) CPL=0 with EIP in [0x00100000, 0x00200000] (kernel image).
in_user=0
in_kern=0
if [ "$cpl" = "3" ]; then
    case "$cs" in
        001b|001B)
            [ "$eip_dec" -ge $((16#400000)) ] && [ "$eip_dec" -le $((16#400100)) ] && in_user=1
            ;;
    esac
fi
if [ "$cpl" = "0" ]; then
    [ "$eip_dec" -ge $((16#100000)) ] && [ "$eip_dec" -le $((16#200000)) ] && in_kern=1
fi
if [ $in_user -eq 0 ] && [ $in_kern -eq 0 ]; then
    echo "FAIL: CPU not in expected state. CPL=$cpl CS=$cs EIP=$eip"
    ok=0
fi

if [ $ok -ne 1 ]; then
    sed -n '1,40p' "$regs"
    exit 1
fi
exit 0
