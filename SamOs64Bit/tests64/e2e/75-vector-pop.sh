#!/usr/bin/env bash
# e2e/75-vector-pop.sh - vector_pop + vector_count round trip.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 94 -eq 1 "vector_pop + vector_count round trip"
echo "PASS: e2e/75 vector_pop"
