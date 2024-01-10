#!/usr/bin/env bash
# e2e/30-graphics.sh - graphics_screen_info() returns a root with
# non-zero width and height (proves UEFI framebuffer injection
# from L86 landed and graphics_setup ran).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_value 37 -eq 1 "graphics_screen_info() non-NULL"
expect_stage_value 38 -eq 1 "screen width + height > 0"
echo "PASS: e2e/30 graphics"
