#!/bin/bash
# tests64/L23-paging-switch-and-drain.sh
#
# Lecture 23 - multi-heap part 3.
#
# What changed:
#   * heap.c block indexes widened from int to int64_t. Two
#     latent bugs fixed: heap_get_start_block returning a
#     partial-run start when total_blocks was not actually
#     reached; heap_mark_blocks_taken dropping HAS_NEXT one
#     block too early so heap_free stopped short.
#   * kheap_init: minimal heap now spans the WHOLE chosen E820
#     region. Sub-heaps clamped above MINIMAL_HEAP_ADDRESS.
#   * kmalloc no longer panics on NULL; callers decide.
#   * paging_map_e820_memory_regions unconditionally identity-
#     maps the first 1 MiB so VGA/BIOS/boot stay addressable
#     across a paging_switch.
#   * kernel_main: panic on paging_desc_new NULL, paging_switch,
#     drain loop. Drain is 1 MiB chunks (SamOs deviation from
#     upstream's 4 KiB to keep the test under a second).
#
# We assert the "Memory wasted" token appears - that proves:
#   - paging_switch to the new descriptor did not fault
#     (VGA write that produced "Memory wasted" would have
#     crashed if 0xB8000 was unmapped)
#   - the multiheap actually exhausts (heap_get_start_block
#     correctly returns -ENOMEM when no run is available)
#   - kmalloc returning NULL is observable (no panic)

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
