#!/usr/bin/env bash
# e2e/50-elf-entry-phnum.sh - ELF header fields (entry pointer
# and program header count) parsed correctly out of BLANK.ELF.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 64 -eq 1 "elf_get_entry_ptr non-NULL"
expect_stage_value 65 -eq 1 "ELF header e_phnum > 0"
echo "PASS: e2e/50 elf header fields"
