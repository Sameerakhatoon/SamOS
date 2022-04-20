#!/usr/bin/env bash
# e2e/73-bmp-format.sh - L89 BMP image format registered with the
# graphics image system.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 92 -eq 1 "graphics_image_format_get(\"image/bmp\") non-NULL"
echo "PASS: e2e/73 bmp format"
