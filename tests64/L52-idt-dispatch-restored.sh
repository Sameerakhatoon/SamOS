#!/bin/bash
# tests64/L52-idt-dispatch-restored.sh
#
# Lecture 52 - IDT dispatch path restored.
#
# interrupt_handler / idt_handle_exception / idt_clock /
# isr80h_handler bodies are back to the task-aware
# kernel_page + state save + callback + task_page dance.
# IRQ0 (timer) is now wired to idt_clock.
#
# Interrupts are still disabled (no sti), so none of this
# fires yet. Smoke tokens unchanged from L50/L51.

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
for tok in 'Hello 64-bit!' 'multiheap ready' 'tss ready'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
