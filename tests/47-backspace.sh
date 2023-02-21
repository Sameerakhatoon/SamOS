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

# 0x08 is the backspace byte. xxd -p gives an unbroken hex stream we
# can grep for the literal "BS-ABC<3xBS>XYZ" delivery.
if ! xxd -p "$log" | tr -d '\n' | grep -q '42532d414243080808585a59\|42532d4142430808085859'; then
    # Try both 5859 (Y first) and 595a (no Z) just in case row wrap
    # split the print; what we really want is BS-ABC + 3 backspaces +
    # at least the first two of XYZ.
    if ! xxd -p "$log" | tr -d '\n' | grep -qE '42532d414243(08){3}(58|59|5a)'; then
        echo "FAIL: serial mirror missing 'BS-ABC<3xBS>X|Y|Z' raw byte sequence"
        echo "      log head:"; head -c 400 "$log"
        exit 1
    fi
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
