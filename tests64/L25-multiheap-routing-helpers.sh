#!/bin/bash
# tests64/L25-multiheap-routing-helpers.sh
#
# Lecture 25 - multi-heap part 5: address-routing scaffolding.
#
# heap_is_address_within_heap (range check),
# multiheap_get_heap_for_address (linear scan over sub-heaps),
# multiheap_get_max_memory_end_address (highest eaddr),
# multiheap_is_address_virtual + virtual_address_to_physical
# (groundwork for the defragment-by-paging arena above the
# highest physical sub-heap).
#
# None of these are called from kernel_main yet, so the runtime
# behaviour matches L23 / L24: paging_switch survives, drain
# exhausts, "Memory wasted" prints.
#
# A failure here would mean we broke the multiheap during the
# refactor.

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
