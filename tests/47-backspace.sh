#!/bin/bash
# tests/47-backspace.sh
#
# Ch 118 - behavioural test for terminal_backspace.
#
# kernel_main loads a blank.elf instance with argv[0]="BS". Its main
# calls print("BS-ABC\b\b\bXYZ\n"). The kernel's print loop hands each
# char (including the three 0x08 bytes) to terminal_writechar, which
# routes \b through terminal_backspace. Step-by-step cursor walk:
#
#   "BS-ABC"  -> cursor at col 6 (cells 0..5 are B,S,-,A,B,C)
#   \b \b \b  -> backspace 3 times, blanks cells 5,4,3, leaves cursor 3
#   "XYZ"     -> writes X,Y,Z over cells 3,4,5
#   "\n"      -> newline
#
# So the final row reads "BS-XYZ". We assert that:
#   1) the serial mirror saw "BS-ABC" then 3x 0x08 then "XYZ" (the
#      user-side string was actually delivered to print()), and
#   2) the visible VGA cell row that BS landed on reads "BS-XYZ" (the
#      kernel's terminal_backspace cursor math is correct).

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

# Phase 1: serial mirror sees the raw "BS-ABC\b\b\bXYZ" delivery.
log=$(mktemp)
trap 'rm -f "$log" "$vga"' EXIT

run_kernel_capture "$log" 'BS-ABC' 'Abc!'

# 0x08 is the backspace byte. terminal_writechar now mirrors every char
# (including the backspace AND the substitute space that terminal_back-
# space writes over the deleted cell) to COM1. So the expected stream
# after "BS-ABC" (42 53 2d 41 42 43) is three pairs of 08 20 (backspace,
# then the space the backspace handler writes), then X Y Z (58 59 5a).
hex=$(xxd -p "$log" | tr -d '\n')
if ! echo "$hex" | grep -qE '42532d414243(0820){3}58595a'; then
    echo "FAIL: serial mirror missing 'BS-ABC' + 3x(BS+space) + 'XYZ' raw byte sequence"
    echo "      log head:"; head -c 400 "$log"
    exit 1
fi

# Phase 2: confirm the visible row reads "BS-XYZ" (terminal_backspace
# rewrote cells in place). Use the marker-sync helper to pmemsave VGA
# right after BS prints, before scrolling pushes its row off the top.
vga=$(mktemp)
log2=$(mktemp)
trap 'rm -f "$log" "$vga" "$log2"' EXIT
run_kernel_inspect "$vga" "$log2" 'BS-ABC' '' "pmemsave 0xb8000 4096 \"$vga\""

chars=$(od -An -v -tx1 -w1 "$vga" \
            | awk 'NR%2==1 {printf "%s\n", $1}' \
            | xxd -r -p \
            | tr '\0' ' ')

if ! echo "$chars" | grep -q 'BS-XYZ'; then
    echo "FAIL: VGA buffer never showed the post-backspace 'BS-XYZ' row"
    echo "      first 800 chars: $(echo "$chars" | head -c 800)"
    exit 1
fi

exit 0
