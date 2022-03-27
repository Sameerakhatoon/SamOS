#!/bin/bash
# tests64/L22-kzalloc-unstubbed.sh
#
# Lecture 22 - multi-heap part 2: page-aligned heap regions,
# kzalloc unstubbed, new kpalloc / kpzalloc paging-defragment
# entry points.
#
# The visible side-effect: L21's "paging desc NULL (kzalloc
# stub)" marker now becomes "paging map built", because
# paging_desc_new (which calls kzalloc twice) now returns a real
# descriptor and paging_map_e820_memory_regions actually runs.

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
for tok in 'Hello 64-bit!' 'e820 total:' 'ABC' 'L21 paging map built'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done

# Negative: the stub marker should NOT be present anymore.
if echo "$chars" | grep -q 'L21 paging desc NULL'; then
    echo "FAIL: stub marker still present (kzalloc regression?)"
    ok=0
fi

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
