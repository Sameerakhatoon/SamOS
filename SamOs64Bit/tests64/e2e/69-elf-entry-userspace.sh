#!/usr/bin/env bash
# e2e/69-elf-entry-userspace.sh - L71 BLANK.ELF's entry point
# falls in the user virtual range (>= 0x400000).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 88 -eq 1 "ELF entry pointer in user-virt range"
echo "PASS: e2e/69 elf entry userspace"
