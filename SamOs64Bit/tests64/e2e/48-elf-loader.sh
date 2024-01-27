#!/usr/bin/env bash
# e2e/48-elf-loader.sh - L65 ELF loader end to end: elf_load +
# elf_validate_loaded + elf_close on @:/BLANK.ELF.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 62 -eq 1 "elf_load + validate + close on BLANK.ELF"
echo "PASS: e2e/48 elf loader"
