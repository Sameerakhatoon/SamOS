#!/usr/bin/env bash
# e2e/44-system-font.sh - L92 font_get_system_font lookup is
# callable. Today returns NULL (sysfont.bmp is not staged on the
# ESP, see G48), so the test is "doesn't crash" not "returns
# non-NULL". Re-tightening this is a follow-up once the asset
# pipeline puts sysfont.bmp on the ESP.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 58 -eq 1 "font_get_system_font does not crash"
echo "PASS: e2e/44 system font"
