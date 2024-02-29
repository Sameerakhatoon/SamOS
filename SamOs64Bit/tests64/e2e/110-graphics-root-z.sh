#!/usr/bin/env bash
# e2e/110-graphics-root-z.sh - root graphics_info has z_index 0
# (the L141 stacking-order contract).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 148 -eq 1 "graphics_screen_info()->z_index == 0"
echo "PASS: e2e/110 graphics root z"
