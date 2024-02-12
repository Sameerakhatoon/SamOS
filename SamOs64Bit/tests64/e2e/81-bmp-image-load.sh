#!/usr/bin/env bash
# e2e/81-bmp-image-load.sh - L89 graphics_image_load decodes the
# staged sysfont.bmp through the BMP loader.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 102 -eq 1 "graphics_image_load(@:/sysfont.bmp) non-NULL"
echo "PASS: e2e/81 bmp image load"
