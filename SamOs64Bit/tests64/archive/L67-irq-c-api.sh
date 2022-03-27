#!/bin/bash
# tests64/L67-irq-c-api.sh
#
# Lecture 67 - IRQ_enable / IRQ_disable C wrappers around the
# 8259 mask register.
#
# Nothing in kernel_main calls them yet. L68 will call
# IRQ_enable(IRQ_KEYBOARD) to actually turn on scancode
# delivery. For L67 we just confirm the file builds and links.

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

[ -f build/idt/irq.o ] || { echo "FAIL: build/idt/irq.o missing"; exit 1; }

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
for tok in 'tss ready' 'user enter'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
