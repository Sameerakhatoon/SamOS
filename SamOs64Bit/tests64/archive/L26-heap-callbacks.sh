#!/bin/bash
# tests64/L26-heap-callbacks.sh
#
# Lecture 26 - per-heap callback handlers.
#
# struct heap gains two function-pointer fields. heap_mark_blocks_
# taken and heap_mark_blocks_free fire them per block when set.
# No heap registers callbacks yet, so runtime behaviour is
# unchanged.
#
# Test asserts the L23-style tokens still appear and the
# multiheap drains cleanly with the new per-block hook logic in
# the loop. A null-check bug (e.g. unconditional callback call)
# would crash before "Memory wasted".

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
