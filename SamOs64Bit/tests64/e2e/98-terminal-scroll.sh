#!/usr/bin/env bash
# e2e/98-terminal-scroll.sh - L98 terminal scroll: 30 newlines
# through a 25-row terminal do not crash, and a 3-row
# terminal really scrolls: an 'X' drawn on row 0 is gone after three
# newlines (row 0 shows the terminal's own background) and the cursor
# stays on the last row. Both checks share marker 130.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 130 -eq 1 "30 newlines through terminal_write round trip"
echo "PASS: e2e/98 terminal scroll"
