#!/usr/bin/env bash
# e2e/28-e820.sh - E820 memory map walked successfully + reports
# a reasonable accessible-memory total.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_value 34 -eq 1 "e820_total_entries() > 0"
expect_stage_value 35 -eq 1 "e820_total_accessible_memory() > 1 MiB"
echo "PASS: e2e/28 e820"
