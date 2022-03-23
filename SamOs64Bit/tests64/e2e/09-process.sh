#!/usr/bin/env bash
# e2e/09-process.sh - process_system_init built the slot vector.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_reached 9 "process_system_init returned"
echo "PASS: e2e/09 process"
