#!/usr/bin/env bash
# e2e/16-fs-fread.sh - VFS fread reads ELF magic bytes from the
# first 4 bytes of BLANK.ELF.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_value 20 -eq 1 "fread first 4 bytes == 0x7F 'E' 'L' 'F'"
echo "PASS: e2e/16 fs fread"
