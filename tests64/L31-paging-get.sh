#!/bin/bash
# tests64/L31-paging-get.sh
#
# Lecture 31 - paging_get real implementation.
#
# The L29 stub returned NULL; that broke paging_get_physical_
# address and thereby multiheap_free's virtual-arena branch.
# L31 implements the real PML4 -> PDPT -> PD -> PT walk so the
# free path can resolve a virtual-arena pointer back to its
# physical backing.
#
# kernel_main does not exercise paging_get yet (kfree is still
# a no-op), so VGA tokens match L23..L30.

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
