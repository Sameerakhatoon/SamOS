#!/bin/bash
# tests/13-kmalloc-works.sh
#
# Ch 48 - kernel_main runs a kmalloc smoke probe after kheap_init: two
# distinct allocations (km1=100B, km2=8000B), then kfree(km1), then a
# third allocation of the same size as km1. The probe prints each
# returned address in hex. Expectations:
#   km1 starts at SAMOS_HEAP_ADDRESS  (0x01000000)
#   km2 is at km1 + one block         (0x01001000)
#   km3 reuses km1's slot after free  (0x01000000)

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

dump=$(mktemp)
cmd=$(mktemp)
trap 'rm -f "$dump" "$cmd"' EXIT

printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump" > "$cmd"

( sleep 7; cat "$cmd" ) | timeout 25 qemu-system-x86_64 \
        -hda bin/os.bin \
        -m 256 \
        -accel tcg \
        -display none \
        -vga std \
        -monitor stdio \
        -no-reboot \
        > /dev/null 2>&1

chars=$(od -An -v -tx1 -w1 "$dump" \
            | awk 'NR%2==1 {printf "%s\n", $1}' \
            | xxd -r -p \
            | tr '\0' ' ')

ok=1
# After Ch 51 paging setup the kernel heap uses 1025 blocks for the
# page directory + 1024 page tables. Ch 60 then creates a disk_stream
# (1 block) before the kmalloc smoke probe runs, and diskstreamer_close
# kfrees that block so the block count is back to 1025 by the time the
# probe runs (kfree marks blocks free for the next alloc).
# So km1 lands at 0x01000000 + 1025 * 0x1000 = 0x01402000.
echo "$chars" | grep -q 'km1=01402000' || { echo "FAIL: km1 != 0x01402000"; ok=0; }
echo "$chars" | grep -q 'km2=01403000' || { echo "FAIL: km2 != 0x01403000"; ok=0; }
echo "$chars" | grep -q 'km3=01402000' || { echo "FAIL: km3 != 0x01402000 (free+realloc reuse)"; ok=0; }

if [ $ok -ne 1 ]; then
    echo "      first 600 chars: $(echo "$chars" | head -c 600)"
    exit 1
fi

exit 0
