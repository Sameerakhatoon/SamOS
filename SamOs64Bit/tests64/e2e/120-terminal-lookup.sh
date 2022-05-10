#!/usr/bin/env bash
# e2e/120-terminal-lookup.sh - terminal_get_at_screen_position
# is callable.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 164 -eq 1 "terminal_get_at_screen_position safe"
echo "PASS: e2e/120 terminal lookup"
