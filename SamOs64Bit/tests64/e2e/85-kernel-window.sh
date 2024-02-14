#!/usr/bin/env bash
# e2e/85-kernel-window.sh - L154/L136 kernel-side window_create
# + window_redraw + window_position_set.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 109 -eq 1 "kernel window_create returns non-NULL"
expect_stage_value 110 -eq 1 "window_redraw runs without crash"
expect_stage_value 111 -eq 1 "window_position_set returns 0"
echo "PASS: e2e/85 kernel window create / redraw / position"
