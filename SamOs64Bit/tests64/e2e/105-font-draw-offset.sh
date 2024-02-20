#!/usr/bin/env bash
# e2e/105-font-draw-offset.sh - L95 font_draw_text at a non-zero
# screen offset.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 141 -eq 1 "font_draw_text(g, f, 16, 32, ...) returns >= 0"
echo "PASS: e2e/105 font draw at offset"
