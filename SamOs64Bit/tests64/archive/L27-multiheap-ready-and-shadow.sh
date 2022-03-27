#!/bin/bash
# tests64/L27-multiheap-ready-and-shadow.sh
#
# Lecture 27 - multiheap_ready + virtual-arena shadow heaps.
#
# multiheap_ready builds a shadow heap for each
# DEFRAGMENT_WITH_PAGING sub-heap, identity-maps the shadow's
# virtual range into the live PML4 as not-present, and hooks the
# free callback. The current-descriptor getter
# paging_current_descriptor is added in this lecture too.
#
# Nothing calls multiheap_ready at L27 (per upstream). Runtime
# behaviour matches L23..L26.

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
