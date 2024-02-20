#!/usr/bin/env bash
# e2e/101-graphics-rect-redraw.sh - L99 graphics_draw_rect + L93
# graphics_redraw_region both round-trip without crash.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 133 -eq 1 "graphics_draw_rect"
expect_stage_value 138 -eq 1 "graphics_redraw_region"
echo "PASS: e2e/101 graphics rect + redraw region"
