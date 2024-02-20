#!/usr/bin/env bash
# e2e/96-e820-largest.sh - L18 e820_largest_free_entry returns
# a non-NULL entry.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 128 -eq 1 "e820_largest_free_entry returns non-NULL"
echo "PASS: e2e/96 e820 largest"
