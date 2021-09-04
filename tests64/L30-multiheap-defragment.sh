#!/bin/bash
# tests64/L30-multiheap-defragment.sh
#
# Lecture 30 - multi-heap part 9: real second-pass allocator,
# block-count bookkeeping on struct heap, ready/can_add gates.
#
# struct heap gains total / free / used counters maintained by
# heap_malloc_blocks and heap_mark_blocks_free.
#
# multiheap_alloc_paging finds a paging-defragment sub-heap with
# enough free physical blocks and allocates a virtual range from
# the shadow.
#
# multiheap_alloc_second_pass really defragments now: pick the
# virtual range, then per-block heap_zalloc the physical backing
# and paging_map the alias.
#
# At L30 nothing routes through any of this yet (kmalloc is
# multiheap_alloc which is first-pass only). Runtime tokens
# match L23..L29.

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
