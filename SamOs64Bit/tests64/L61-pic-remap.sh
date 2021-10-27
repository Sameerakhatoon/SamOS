#!/bin/bash
# tests64/L61-pic-remap.sh
#
# Lecture 61 - PIC remap.
#
# The 8259 master/slave PICs are reprogrammed so IRQ0..7 land
# at vectors 0x20..0x27 (master) and IRQ8..15 at 0x28..0x2F
# (slave), clear of the CPU exception range 0x00..0x1F.
#
# All IRQs masked except IRQ2 (cascade) - so no IRQs actually
# fire yet. We assert:
#   - "user enter" still appears (ring-3 entry survived)
#   - "Panic Exception" is GONE (no more timer/#DF collision)

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

dump=$(mktemp)
trap 'rm -f "$dump"' EXIT

( sleep 5; printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump" ) | timeout 12 \
    qemu-system-x86_64 -hda bin/os.bin -m 256 -accel tcg -display none -vga std \
        -monitor stdio -no-reboot > /dev/null 2>&1

chars=$(od -An -v -tx1 -w1 "$dump" \
            | awk 'NR%2==1 {print $1}' \
            | xxd -r -p \
            | tr '\0' ' ')

ok=1
for tok in 'tss ready' 'Loading program...' 'user enter'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done
# The post-PIC-remap world should NOT show Panic Exception any
# more (the timer is masked, so no IRQ0 fires).
if echo "$chars" | grep -q 'Panic Exception'; then
    echo "FAIL: 'Panic Exception' present - PIC remap or mask didn't take?"
    ok=0
fi

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
