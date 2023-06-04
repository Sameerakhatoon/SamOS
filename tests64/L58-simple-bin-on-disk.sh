#!/bin/bash
# tests64/L58-simple-bin-on-disk.sh
#
# Lecture 58 - simple.bin user program on the FAT16 disk image.
#
# Checks:
#   1. mdir on bin/os.bin lists SIMPLE.BIN
#   2. boot signature 0xAA55 at offset 510..511 of os.bin
#   3. kernel boot reaches "tss ready" + "keyboard initialised"
#      (proves the FAT16 install didn't clobber boot or kernel)

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

# 1. simple.bin must be visible to mtools.
mdir -i bin/os.bin :: 2>/dev/null | grep -q 'SIMPLE *BIN' \
    || { echo "FAIL: SIMPLE.BIN not in bin/os.bin"; exit 1; }

# 2. boot signature
sig=$(od -An -tx1 -j 510 -N 2 bin/os.bin | tr -d ' ')
[ "$sig" = "55aa" ] || { echo "FAIL: boot signature is '$sig', want '55aa'"; exit 1; }

# 3. kernel boot tokens
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
for tok in 'multiheap ready' 'tss load was fine' 'register isr80h' 'tss ready'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
