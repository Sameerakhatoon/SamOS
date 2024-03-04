#!/usr/bin/env bash
# e2e/123-graphics-relative.sh - graphics_info_create_relative
# returns a non-NULL child graphics_info.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 167 -eq 1 "graphics_info_create_relative returns child"
echo "PASS: e2e/123 graphics relative"
