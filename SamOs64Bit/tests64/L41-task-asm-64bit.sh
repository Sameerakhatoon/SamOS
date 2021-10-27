#!/bin/bash
# tests64/L41-task-asm-64bit.sh
#
# Lecture 41 - task.asm rewritten for 64-bit.
#
# task_return, restore_general_purpose_registers, user_registers
# all rebuilt under [BITS 64] / SysV. Plus L40-fix: USER_CODE_
# SEGMENT / USER_DATA_SEGMENT macros corrected (upstream had
# them swapped relative to the L39 GDT).
#
# task.asm is not in the build yet. Same tokens as L38..L40.

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
for tok in 'Hello 64-bit!' 'multiheap ready' 'hello' 'Divide by zero error'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
