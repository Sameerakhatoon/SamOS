#!/usr/bin/env bash
# e2e/109-e820-region-sum.sh - total accessible memory >= the
# largest individual free region (sanity check on the E820 walk).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 147 -eq 1 "total memory >= largest E820 region"
echo "PASS: e2e/109 e820 sanity"
