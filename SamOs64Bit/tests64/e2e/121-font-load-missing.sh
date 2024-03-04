#!/usr/bin/env bash
# e2e/121-font-load-missing.sh - font_load with a missing file
# returns NULL.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 165 -eq 1 "font_load(@:/NOSUCH.BMP) == NULL"
echo "PASS: e2e/121 font load missing"
