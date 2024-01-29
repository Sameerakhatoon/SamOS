#!/usr/bin/env bash
# e2e/55-tsc-busy-loop.sh - tsc_microseconds does not regress
# (later read >= earlier read).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 71 -eq 1 "tsc_microseconds non-regressing across busy loop"
echo "PASS: e2e/55 tsc busy loop"
