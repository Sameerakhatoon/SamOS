#!/usr/bin/env bash
# e2e/10-keyboard.sh - keyboard_init inserted the PS/2 driver.
# Verifies G36 (keyboard-before-stage2 ordering) is preserved -
# stage 10 must be reached before stage 11 (window stage 2).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_reached 10 "keyboard_init returned"
expect_stage_reached 11 "window stage 2 done (after keyboard)"
echo "PASS: e2e/10 keyboard"
