#!/usr/bin/env bash
# e2e/11-window.sh - window_system_initialize_stage2 completed
# (with the L196 fix: registers a keyboard listener that needs
# keyboard_default() non-NULL).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 15
expect_stage_reached 11 "window stage 2 done"
echo "PASS: e2e/11 window"
