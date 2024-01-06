#!/bin/bash
# tests64/L21-paging-map-e820.sh
#
# Lecture 21 - paging_map_e820_memory_regions.
#
# kernel_main calls paging_desc_new(PAGING_MAP_LEVEL_4). At L20
# kzalloc is stubbed to NULL, so paging_desc_new returns NULL and
# the NULL guard prints "L21 paging desc NULL (kzalloc stub)".
# Once L22 unstubs kzalloc the same code path will instead print
# "L21 paging map built" and the test will switch tokens.
#
# The interesting code under test is the function body itself:
# paging_map_e820_memory_regions exists, compiles, links, and is
# wired through paging.h. The runtime exercise has to wait for
# the heap to support zalloc.

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

dump=$(mktemp)
trap 'rm -f "$dump"' EXIT

( sleep 2; printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump" ) | timeout 12 \
    qemu-system-x86_64 -hda bin/os.bin -m 256 -accel tcg -display none -vga std \
        -monitor stdio -no-reboot > /dev/null 2>&1

chars=$(od -An -v -tx1 -w1 "$dump" \
            | awk 'NR%2==1 {print $1}' \
            | xxd -r -p \
            | tr '\0' ' ')

ok=1
for tok in 'Hello 64-bit!' 'e820 total:' 'ABC'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done

# At L21, accept either of the two possible outcomes. Once L22
# unstubs kzalloc we'll tighten this to require "L21 paging map
# built".
if echo "$chars" | grep -q 'L21 paging desc NULL'; then
    : # deferred bug, expected at L21
elif echo "$chars" | grep -q 'L21 paging map built'; then
    : # success, expected at L22+
else
    echo "FAIL: missing both L21 marker strings"
    ok=0
fi

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
