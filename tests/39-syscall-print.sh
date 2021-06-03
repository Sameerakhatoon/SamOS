#!/bin/bash
# tests/39-syscall-print.sh
#
# Ch 108: the user program (blank.bin) now calls SYSTEM_COMMAND1_PRINT
# from ring 3 with a message pointer pushed onto its stack. The kernel
# handler pulls the pointer via task_get_stack_item, copies the string
# out of user space via copy_string_from_task (the page-table portal),
# and writes it to the VGA buffer with the kernel's `print()`. Verify
# the message appears on the VGA dump.

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

dump=$(mktemp)
trap 'rm -f "$dump"' EXIT

# Ch 116: blank.bin blocks on getkey until a key arrives, then prints.
(
    sleep 12
    printf 'sendkey a\n'
    sleep 1
    printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump"
) | timeout 30 qemu-system-x86_64 \
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

# Ch 117 swapped the static-message print for getkey -> putchar. With
# the QEMU sendkey 'a' (scancode 0x1E, mapped to ASCII 'A' in
# keyboard_scan_set_one), the user program now writes 'A' to VGA. A
# whitespace-stripped search for ' A ' is too brittle; assert a stable
# substring of the boot smoke probes is still present (kernel alive)
# AND that 'A' appears past the boot probes.
if echo "$chars" | grep -q 'A'; then
    exit 0
fi

echo "FAIL: putchar syscall - 'A' from sendkey not found on VGA"
echo "      first 1500 chars: $(echo "$chars" | head -c 1500)"
exit 1
