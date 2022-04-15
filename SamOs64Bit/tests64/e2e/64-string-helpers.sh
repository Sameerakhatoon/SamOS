#!/usr/bin/env bash
# e2e/64-string-helpers.sh - strlen + strncpy work.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 75 -eq 1 "strlen(\"hello\") == 5"
expect_stage_value 76 -eq 1 "strncpy reserves null terminator (count-1 byte copy)"
echo "PASS: e2e/64 string helpers"
