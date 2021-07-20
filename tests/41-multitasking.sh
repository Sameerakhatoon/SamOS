#!/bin/bash
# tests/41-multitasking.sh
#
# Ch 150 grand-finale sentinel - validates the whole post-Ch-100 chain end
# to end: ELF loader (Ch 121-126), user-mode syscalls (cmd 1 print, cmd 8
# get_program_arguments), c_start bootstrap (Ch 144), process_inject_arguments
# (Ch 143), PIT IRQ -> idt_clock -> task_next preemption (Ch 150), and the
# G05/G07/G08/G09 set of guards around the interrupt path.
#
# Mechanism: kernel_main loads TWO blank.elf instances with argv "Testing!"
# and "Abc!". Each calls `print(argv[0])` in a tight loop. If multitasking
# works, VGA at a single snapshot has BOTH strings interleaved. If only one
# appears the scheduler isn't running task 2 (G09 sentinel).

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

dump=$(mktemp)
trap 'rm -f "$dump"' EXIT

(
    sleep 5
    printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump"
) | timeout 25 qemu-system-x86_64 \
        -hda bin/os.bin \
        -m 256 \
        -accel tcg \
        -display none \
        -vga std \
        -monitor stdio \
        -no-reboot \
        > /dev/null 2>&1

chars=$(od -An -v -tx1 -w1 "$dump" \
            | awk 'NR%2==1 {printf "%s\n", $1}' \
            | xxd -r -p \
            | tr '\0' ' ')

testing=$(echo "$chars" | grep -o 'Testing' | wc -l)
abc=$(echo "$chars"     | grep -o 'Abc'     | wc -l)

ok=1
if [ "$testing" -lt 5 ]; then
    echo "FAIL: only $testing 'Testing!' tokens on VGA - task 1 (argv[0]=Testing!) may not be running"
    ok=0
fi
if [ "$abc" -lt 5 ]; then
    echo "FAIL: only $abc 'Abc' tokens on VGA - task 2 (argv[0]=Abc!) not getting scheduled (G09 sentinel)"
    ok=0
fi

if [ $ok -ne 1 ]; then
    echo "      VGA snapshot first 600 chars:"
    echo "      $(echo "$chars" | head -c 600)"
    exit 1
fi
exit 0
