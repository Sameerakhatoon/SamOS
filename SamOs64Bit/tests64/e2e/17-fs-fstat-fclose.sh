#!/usr/bin/env bash
# e2e/17-fs-fstat-fclose.sh - VFS fstat returns a positive file
# size for BLANK.ELF, and fclose succeeds.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_value 21 -eq 1 "fstat reports BLANK.ELF size > 0"
expect_stage_value 22 -eq 1 "fclose succeeds"
echo "PASS: e2e/17 fs fstat + fclose"
