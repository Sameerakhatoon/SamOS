#!/usr/bin/env bash
# e2e/56-e820-type1.sh - at least one E820 entry has type == 1
# (usable RAM).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 72 -eq 1 "at least one E820 entry of type 1 (usable)"
echo "PASS: e2e/56 e820 type1"
