#!/bin/bash
# tests64/L16-dynamic-kheap.sh
#
# Lecture 16 - kheap_init takes a runtime size + heap accounting.
#
# After the L15 KBC token, kernel_main now prints:
#   heap size: 104857600   (100 MiB - SAMOS_HEAP_SIZE_BYTES)
#   heap used: 839680      (1 kmalloc(50) + the PML4/PDPT/PD/PT
#                          tree built by paging_map_range:
#                          1 desc + 1 PML4 + 1 PDPT + 1 PD +
#                          200 PTs + 1 user buf = 205 blocks
#                          x 4096 = 839680)
#   heap free: 104017920   (104857600 - 839680)
#
# heap size proves kheap_init(SAMOS_HEAP_SIZE_BYTES) accepted the
# arg + heap_total_size returns the right value.
# heap used / heap free prove heap_total_used iterates the entry
# table correctly and the bookkeeping is self-consistent.

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
echo "$chars" | grep -q 'heap size: 104857600' \
    || { echo "FAIL: heap size line missing or wrong"; ok=0; }
echo "$chars" | grep -q 'heap used: 839680' \
    || { echo "FAIL: heap used line missing or wrong"; ok=0; }
echo "$chars" | grep -q 'heap free: 104017920' \
    || { echo "FAIL: heap free line missing or wrong"; ok=0; }

# Self-consistency: even if the magic numbers drift due to a future
# paging-mapper refactor, used + free must always equal size.
size=$(echo "$chars" | grep -oE 'heap size: [0-9]+' | grep -oE '[0-9]+')
used=$(echo "$chars" | grep -oE 'heap used: [0-9]+' | grep -oE '[0-9]+')
free=$(echo "$chars" | grep -oE 'heap free: [0-9]+' | grep -oE '[0-9]+')
if [ -n "$size" ] && [ -n "$used" ] && [ -n "$free" ]; then
    sum=$((used + free))
    if [ "$sum" != "$size" ]; then
        echo "FAIL: used + free ($sum) != size ($size)"
        ok=0
    fi
fi

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
