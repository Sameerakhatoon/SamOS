#!/usr/bin/env bash
# e2e/66-itoa.sh - L16 / L106 itoa(123) returns "123".
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 85 -eq 1 "itoa(123) == \"123\""
echo "PASS: e2e/66 itoa"
