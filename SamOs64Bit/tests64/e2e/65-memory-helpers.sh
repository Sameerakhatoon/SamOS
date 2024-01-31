#!/usr/bin/env bash
# e2e/65-memory-helpers.sh - memset + memcpy + memcmp work.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 77 -eq 1 "memset writes the fill byte"
expect_stage_value 78 -eq 1 "memcpy roundtrips"
expect_stage_value 79 -eq 1 "memcmp returns 0 on equal / non-zero on differ"
echo "PASS: e2e/65 memory helpers"
