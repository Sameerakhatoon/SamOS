#!/usr/bin/env bash
# e2e/67-strncmp.sh - strncmp("abc", "abd", 3) is non-zero.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 86 -eq 1 "strncmp equal->0, differ->!=0"
echo "PASS: e2e/67 strncmp"
