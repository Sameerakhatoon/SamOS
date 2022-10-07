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
# After Ch 51 paging: 1025 blocks (1 directory + 1024 page tables).
# Ch 68 fat16_resolve adds: fat_private (1) + 3 streams (3) + root-dir
# items buffer (1) + temp resolver stream (1, then closed). Slot 1031
# becomes free.
# Ch 70 then runs three fopen probes before kmalloc smoke:
#   #1 "0:/anything", "r":  path_root + segment string + path_part +
#                           trailing-string (freed) + descriptor.
#                           Slots 1031..1033 + 1034 (trailing freed and
#                           reused for descriptor).
#   #2 "notapath", "r":     rejected by path parser before any alloc.
#   #3 "0:/anything", "z":  path_root + segment string + path_part +
#                           trailing-string (freed). Slots 1035..1037
#                           taken, slot 1038 freed.
# km1's first-fit walk picks slot 1038:
#   0x01000000 + 1038 * 0x1000 = 0x0140E000
echo "$chars" | grep -q 'km1=0140E000' || { echo "FAIL: km1 != 0x0140E000"; ok=0; }
echo "$chars" | grep -q 'km2=0140F000' || { echo "FAIL: km2 != 0x0140F000"; ok=0; }
echo "$chars" | grep -q 'km3=0140E000' || { echo "FAIL: km3 != 0x0140E000 (free+realloc reuse)"; ok=0; }

if [ $ok -ne 1 ]; then
    echo "      first 600 chars: $(echo "$chars" | head -c 600)"
    exit 1
fi

exit 0
