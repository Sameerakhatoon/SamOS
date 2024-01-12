#!/usr/bin/env bash
# e2e/33-user-fread.sh - userland fread via SYSTEM_COMMAND12_FREAD
# reads the ELF magic from BLANK.ELF.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 41 -eq 1 "user fread first 4 bytes == ELF magic"
echo "PASS: e2e/33 user fread"
