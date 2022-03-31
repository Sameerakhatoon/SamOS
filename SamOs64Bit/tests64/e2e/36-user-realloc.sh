#!/usr/bin/env bash
# e2e/36-user-realloc.sh - userland realloc preserves prefix
# (covers Module 2 Mod 2 process_realloc bug fix path; G38).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 46 -eq 1 "user realloc(32 -> 128) preserves first 32 bytes"
echo "PASS: e2e/36 user realloc"
