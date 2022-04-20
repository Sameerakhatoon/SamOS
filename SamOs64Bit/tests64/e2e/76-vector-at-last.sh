#!/usr/bin/env bash
# e2e/76-vector-at-last.sh - vector_at(N-1) returns the last
# pushed element.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 95 -eq 1 "vector_at(count-1) returns last pushed"
echo "PASS: e2e/76 vector_at last"
