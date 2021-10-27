#!/bin/bash
# tests64/L32-multiheap-ready.sh
#
# Lecture 32 - call multiheap_ready from kernel_main.
#
# kheap_post_paging() calls multiheap_ready(kernel_multiheap)
# which:
#   - sets MULTIHEAP_FLAG_IS_READY (gates further adds)
#   - panics if no paging desc is loaded (we paging_switch'd
#     earlier, so this passes)
#   - captures max_end_data_addr from
#     multiheap_get_max_memory_end_address
#   - for every DEFRAGMENT_WITH_PAGING sub-heap:
#       * allocates a shadow heap_table + entries bitmap from
#         the starting heap
#       * heap_create the shadow at virtual address
#         max_end_data_addr + saddr/eaddr
#       * paging_map_to the virtual range as not-present
#       * heap_callbacks_set installs the unmap free callback
#
# We assert the "multiheap ready" token, which proves the entire
# build completed without faulting on any of the page-table
# manipulations.

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
for tok in 'Hello 64-bit!' 'e820 total:' 'ABC' 'multiheap ready'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
