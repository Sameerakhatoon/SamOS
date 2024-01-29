#!/usr/bin/env bash
# e2e/54-fs-fread-8.sh - fread 8 bytes from BLANK.ELF returns the
# ELF magic + ELF64 class + little-endian marker.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 70 -eq 1 "fread(8) returns ELF64 little-endian header"
echo "PASS: e2e/54 fread 8 bytes ELF64 header"
