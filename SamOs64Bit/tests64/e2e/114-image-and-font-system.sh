#!/usr/bin/env bash
# e2e/114-image-and-font-system.sh - unregistered MIME returns
# NULL from graphics_image_format_get; font_get_system_font
# returns a struct after font_load has run.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 157 -eq 1 "graphics_image_format_get(\"image/png\") == NULL"
expect_stage_value 158 -eq 1 "font_get_system_font returns non-NULL after font_load"
echo "PASS: e2e/114 image + font system"
