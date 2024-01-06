#!/bin/bash
# tests64/L20-multiheap.sh
#
# Lecture 20 - multi-heap part 1.
#
# kheap_init now:
#   1) Walks E820, finds first type-1 region big enough for
#      SAMOS_HEAP_SIZE_BYTES (100 MiB).
#   2) Places the minimal heap's bitmap at 0x01000000 and the
#      data pool at 0x01100000 (fixed kernel addresses, not
#      overlapping the E820 buffer anymore).
#   3) Wraps the minimal heap in a multiheap.
#   4) Adds every other type-1 E820 region as additional
#      sub-heaps (heap header + table allocated INSIDE the
#      minimal heap, demonstrating bootstrap).
#
# kmalloc(50) goes through multiheap_alloc -> first-pass walks
# the linked list, succeeds on the minimal heap, returns a
# pointer at the start of the data pool (0x01100000).
#
# Test: banner, e820 line, ABC. (L13/L15/L16 prints are gone -
# upstream commented them out in this same lecture; SamOs
# follows.)

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

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
