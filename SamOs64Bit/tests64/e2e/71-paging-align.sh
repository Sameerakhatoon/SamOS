#!/usr/bin/env bash
# e2e/71-paging-align.sh - paging_align_address rounds up to the
# next 4 KiB boundary.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 90 -eq 1 "paging_align_address(0x1234) == 0x2000"
echo "PASS: e2e/71 paging align"
