#!/bin/bash
# tests64/L29-multiheap-free.sh
#
# Lecture 29 - multiheap_free, paging_get_physical_address.
#
# multiheap_free (the OLD all-heaps teardown) renamed to
# multiheap_free_heap. The short name is now the per-allocation
# free, with a virtual-arena branch that recursively frees each
# block's physical backing via paging_get_physical_address.
#
# paging_get is added as a NULL-returning stub (real impl in
# L31). paging_get_physical_address propagates the NULL through.
#
# Nothing calls multiheap_free at L29 (kheap.c::kfree is still
# a no-op). Runtime tokens unchanged from L23..L28.

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
for tok in 'Hello 64-bit!' 'e820 total:' 'ABC' 'Memory wasted'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
