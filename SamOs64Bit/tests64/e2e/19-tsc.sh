#!/usr/bin/env bash
# e2e/19-tsc.sh - tsc_microseconds advances between two reads
# separated by a small busy loop.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_value 25 -eq 1 "tsc_microseconds advances"
echo "PASS: e2e/19 tsc"
