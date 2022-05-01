#!/usr/bin/env bash
# e2e/98-terminal-scroll.sh - L98 terminal scroll: 30 newlines
# force the terminal to wrap past its 25-row height without
# crashing.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 130 -eq 1 "30 newlines through terminal_write round trip"
echo "PASS: e2e/98 terminal scroll"
