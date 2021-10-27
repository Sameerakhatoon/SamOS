#!/bin/bash
# tests64/L45-task-process-linked.sh
#
# Lectures 42 - 45 - task / process subsystem in the build.
#
# After L40 / L41 / L42 / L43 / L44 staged the source-level
# rewrites and helpers, L45 wires the lot into FILES:
#   build/task/task.o
#   build/task/task.asm.o
#   build/task/process.o
#   build/idt/idt.o (already in from L38)
#   build/memory/paging/paging.o (paging_desc_free added L43)
#
# The link has to find every external symbol:
#   - paging_desc_free (L43)
#   - kernel_desc (L44)
#   - paging_get, paging_get_physical_address (L29/L31)
#   - task_return, restore_general_purpose_registers,
#     user_registers (L41 task.asm)
#   - all the heap / paging primitives we've been building since
#     L13
#
# No task is actually CREATED yet - nothing in kernel_main calls
# task_new. The smoke test just confirms the link works and the
# divide-by-zero IDT path still fires the same way.

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
