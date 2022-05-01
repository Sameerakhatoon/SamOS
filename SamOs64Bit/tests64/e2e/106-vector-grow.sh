#!/usr/bin/env bash
# e2e/106-vector-grow.sh - vector grows past its initial capacity
# (4 -> 10 pushes succeed, count == 10).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 142 -eq 1 "vector grows past initial capacity"
echo "PASS: e2e/106 vector grow"
