#!/usr/bin/env bash
# e2e/83-font-draw.sh - L95 font_draw_text on the root graphics
# returns >= 0.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 103 -eq 1 "font_draw_text returns >= 0"
echo "PASS: e2e/83 font draw text"
