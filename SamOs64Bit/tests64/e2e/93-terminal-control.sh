#!/usr/bin/env bash
# e2e/93-terminal-control.sh - L97/L91 terminal control chars
# newline + backspace round trip through terminal_write.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 124 -eq 1 "terminal_write '\\\\n' returns >= 0"
expect_stage_value 125 -eq 1 "terminal_write '\\\\b' returns >= 0"
echo "PASS: e2e/93 terminal newline + backspace"
