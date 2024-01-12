#!/usr/bin/env bash
# e2e/34-user-fstat-fseek-fclose.sh - the rest of the userland
# file API (fstat, fseek, fclose).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 42 -eq 1 "user fstat reports size > 0"
expect_stage_value 43 -eq 1 "user fseek(0, SEEK_SET) succeeds"
expect_stage_value 44 -eq 1 "user fclose returns"
echo "PASS: e2e/34 user fstat + fseek + fclose"
