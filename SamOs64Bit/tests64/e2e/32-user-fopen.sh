#!/usr/bin/env bash
# e2e/32-user-fopen.sh - userland fopen via SYSTEM_COMMAND10_FOPEN.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 40 -eq 1 "user fopen(@:/blank.elf) returns fd > 0"
echo "PASS: e2e/32 user fopen"
