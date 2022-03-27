#!/bin/bash
# tests64/L24-bitmap-sizing.sh
#
# Lecture 24 - multi-heap part 4: precise bitmap sizing.
#
# The minimal heap's bitmap is now sized to match the actual
# data pool length instead of the fixed 1 MiB budget. Behaviour
# from kernel_main's perspective is unchanged: paging_switch
# still succeeds, drain still exhausts, "Memory wasted" still
# appears.
#
# This test is a smoke test that the resizing did not break
# kheap_init. A failure here would mean either heap_create
# rejected the new bounds (alignment / table-size mismatch) or
# the data pool ended up too small for the drain to terminate
# cleanly.

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
