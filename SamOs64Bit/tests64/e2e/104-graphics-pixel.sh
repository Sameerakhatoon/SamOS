#!/usr/bin/env bash
# e2e/104-graphics-pixel.sh - graphics_pixel_get round trip
# (write a pixel, read it back).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 140 -eq 1 "graphics_pixel_get round trip"
echo "PASS: e2e/104 graphics_pixel_get"
