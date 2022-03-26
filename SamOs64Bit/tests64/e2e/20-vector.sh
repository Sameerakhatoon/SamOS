#!/usr/bin/env bash
# e2e/20-vector.sh - generic vector lifecycle: new, push four
# ints, count, at, free.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_value 26 -eq 1 "vector_new + push + at + free round trip"
echo "PASS: e2e/20 vector"
