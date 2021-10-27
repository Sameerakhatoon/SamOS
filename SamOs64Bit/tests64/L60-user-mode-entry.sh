#!/bin/bash
# tests64/L60-user-mode-entry.sh
#
# Lectures 59 + 60 - load + iretq into ring 3.
#
# kernel_main now ends with process_load_switch + task_run_
# first_ever_task. We must see:
#   - "user enter" (just before task_run_first_ever_task)
#   - "Panic Exception" (the IRQ0 timer fires at vector 0x08
#     which the CPU shares with #DF; our IDT vector 8 is
#     idt_handle_exception which panics. L61 fixes this with
#     the PIC remap.)
#
# Critically the "Panic Exception" appears AFTER "user enter".
# That ordering proves we successfully iretq'd into ring 3,
# the user code (jmp $) ran for some duration, and the timer
# fire then routed back through the IDT.

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

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
