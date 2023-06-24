#!/bin/bash
# tests64/L18-e820.sh
#
# Lecture 18 - boot-time BIOS E820 memory map probe.
#
# boot.asm calls int 0x15/0xE820 in real mode, dumping the
# entries at 0x7E00 and the count at 0x7DFE. kernel.c reads them
# BEFORE kheap_init (because the heap bitmap lives at the same
# address).
#
# Under QEMU -m 256 we expect the usable total to be ~256 MiB
# minus a small chunk reserved for BIOS / EBDA / ROM. Empirically
# QEMU reports 267910144 bytes = 255.49 MiB usable (256 MiB minus
# ~513 KiB reserved at the bottom 640 KiB + ROM).
#
# We assert:
#   - "e820 total: " line is present
#   - the integer is within a sane band: 100 MiB < X < 256 MiB
#     (catches "we read garbage" and "we forgot to filter type==1")

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
echo "$chars" | grep -q 'e820 total:' \
    || { echo "FAIL: e820 line missing"; ok=0; }

total=$(echo "$chars" | grep -oE 'e820 total: [0-9]+' | sed 's/^e820 total: //')
if [ -z "$total" ]; then
    echo "FAIL: could not parse e820 total"
    ok=0
else
    # 100 MiB < total < 256 MiB.
    if [ "$total" -le 104857600 ] || [ "$total" -ge 268435456 ]; then
        echo "FAIL: e820 total $total out of sane range (100 MiB, 256 MiB)"
        ok=0
    fi
fi

# Don't regress earlier lectures.
for tok in 'Hello 64-bit!' 'ABC' 'MBC' 'KBC' 'heap size:' 'heap used:' 'heap free:'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
