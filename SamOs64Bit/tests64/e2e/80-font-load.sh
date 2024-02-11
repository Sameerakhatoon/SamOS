#!/usr/bin/env bash
# e2e/80-font-load.sh - L92 font_load("@:/sysfont.bmp") returns a
# non-NULL font with the expected 9-pixel cell width. Unblocks
# the G48 dependency: a 9x16-cell stub sysfont.bmp is now staged
# on the ESP via build.sh.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 98 -eq 1 "font_load(@:/sysfont.bmp) non-NULL"
expect_stage_value 99 -eq 1 "loaded font reports cell width 9"
echo "PASS: e2e/80 font load"
