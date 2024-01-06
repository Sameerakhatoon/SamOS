#!/bin/bash
# tests64/L13-c-paging.sh
#
# Lecture 13 - C-side 64-bit paging API.
#
# kernel_main now does:
#   1) terminal_initialize + print("Hello 64-bit!\n")
#   2) kheap_init + kmalloc(50) + write "ABC" + print(data)
#   3) paging_desc_new(PAGING_MAP_LEVEL_4)
#   4) paging_map_range(desc, 0, 0, 102400, RW|P) - identity-map
#      ~400 MiB into the new descriptor's PML4
#   5) paging_switch(desc) - load the new PML4 into CR3
#   6) data[0] = 'M' (kmalloc'd buffer at ~0x01000000 must still
#      be reachable through the new map)
#   7) print(data) again
#
# So VGA should show: "Hello 64-bit!" then "ABC" then "MBC".

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
echo "$chars" | grep -q 'Hello 64-bit!' || { echo "FAIL: banner missing"; ok=0; }
echo "$chars" | grep -q 'ABC' || { echo "FAIL: pre-switch ABC missing"; ok=0; }
echo "$chars" | grep -q 'MBC' || { echo "FAIL: post-switch MBC missing (paging_switch broke the buffer?)"; ok=0; }

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
