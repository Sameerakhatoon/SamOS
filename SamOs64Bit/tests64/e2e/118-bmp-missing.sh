#!/usr/bin/env bash
# e2e/118-bmp-missing.sh - graphics_image_load for a missing
# file returns NULL.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 162 -eq 1 "graphics_image_load(@:/NOSUCH.BMP) == NULL"
echo "PASS: e2e/118 bmp missing"
