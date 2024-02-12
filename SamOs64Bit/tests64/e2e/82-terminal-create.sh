#!/usr/bin/env bash
# e2e/82-terminal-create.sh - L96-L100 terminal_create returns
# a struct; terminal_write of an ASCII char succeeds.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 100 -eq 1 "terminal_create returns non-NULL"
expect_stage_value 101 -eq 1 "terminal_write('H') returns >= 0"
echo "PASS: e2e/82 terminal create + write"
