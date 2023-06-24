#!/bin/bash
# tests64/L54-isr80h-linked.sh
#
# Lecture 54 - isr80h command table linked + registered.
#
# All five isr80h sources (isr80h.c, io.c, heap.c, misc.c,
# process.c) added to FILES. kernel_main calls
# isr80h_register_commands() right after the TSS install so
# the command table is populated for any future int 0x80.
#
# SamOs fixed three width-bug spots in the isr80h sources
# that 32-bit got away with:
#   isr80h/io.c   - (intptr_t) intermediate for char round
#                   trips and stack-item void*
#   isr80h/heap.c - (uintptr_t) for the size_t out of a
#                   stack item
#   isr80h/misc.c - same for the two int operands in the sum
#                   syscall demo
#
# Nothing currently sends an int 0x80, so the table is dormant.
# Tokens unchanged from L52/L53.

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
for tok in 'Hello 64-bit!' 'multiheap ready' 'tss ready'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
