#!/usr/bin/env bash
# e2e/100-string-extras.sh - L122 tolower + isdigit + tonumericdigit
# + strnlen_terminator (the rest of the string helpers).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 132 -eq 1 "tolower"
expect_stage_value 135 -eq 1 "isdigit"
expect_stage_value 136 -eq 1 "tonumericdigit"
expect_stage_value 137 -eq 1 "strnlen_terminator"
echo "PASS: e2e/100 string extras"
