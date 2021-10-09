#!/bin/bash
# tests64/L56-tss-loaded-isr80h-registered.sh
#
# Lectures 55 + 56 - ltr the TSS, register isr80h commands.
#
# tss_load(0x38) loads the Task Register with the TSS we
# installed at L50. The GDTR limit was already wide enough
# (the reserved slots went in BEFORE gdt_end), so ltr finds
# the descriptor and doesn't #GP.
#
# isr80h_register_commands() fills the command table.
#
# Two new debug breadcrumbs from upstream's L56 land:
#   "tss load was fine"   - after tss_load
#   "register isr80h"     - after isr80h_register_commands
# Plus the existing "tss ready".

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

dump=$(mktemp)
trap 'rm -f "$dump"' EXIT

( sleep 2; printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump" ) | timeout 15 \
    qemu-system-x86_64 -hda bin/os.bin -m 256 -accel tcg -display none -vga std \
        -monitor stdio -no-reboot > /dev/null 2>&1

chars=$(od -An -v -tx1 -w1 "$dump" \
            | awk 'NR%2==1 {print $1}' \
            | xxd -r -p \
            | tr '\0' ' ')

ok=1
for tok in 'Hello 64-bit!' 'multiheap ready' 'tss load was fine' 'register isr80h' 'tss ready'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
