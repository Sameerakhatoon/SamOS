#!/bin/bash
# tests64/L15-kernel-page.sh
#
# Lecture 15 - kernel paging abstraction.
#
# kernel_main:
#   "Hello 64-bit!"  -> banner
#   "ABC"            -> kmalloc + first print
#   "MBC"            -> data[0]='M' after first paging_switch
#                       (kernel_paging_desc, freshly built)
#   "KBC"            -> data[0]='K' after kernel_page()
#                       (kernel_registers + paging_switch combo)
#
# Tokens "MBC" and "KBC" together prove:
#   - paging_switch(kernel_paging_desc) works
#   - kernel_registers() reloads ds/es/fs/gs without #GP
#   - kernel_page() is a clean no-op when already on the kernel desc
#     (which is the path every fault handler / scheduler entry will
#     take in later lectures).

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
for tok in 'Hello 64-bit!' 'ABC' 'MBC' 'KBC'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
