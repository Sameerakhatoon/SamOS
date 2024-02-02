#!/usr/bin/env bash
# e2e/72-elf-reload.sh - elf_load + close + load again succeeds.
# Catches stateful leaks in the ELF loader (FAT FD cache,
# elfloader malloc not freed, etc.).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 91 -eq 1 "elf_load/close/load again returns 0 both times"
echo "PASS: e2e/72 elf reload"
