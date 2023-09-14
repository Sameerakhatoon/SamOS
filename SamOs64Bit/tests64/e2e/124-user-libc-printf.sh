#!/usr/bin/env bash
# e2e/124-user-libc-printf.sh - userland snprintf/printf run
# inside SamOS by blank.elf: %d %i %u %x %X %p %c %s %ld %lu,
# width / '-' / '0', %%, a trailing lone %, truncation, and the
# printf return value. Value 1 = all passed; 0x100+N = check N in
# blank.c libc_printf_checks failed.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 168 -eq 1 "user snprintf/printf formats"
echo "PASS: e2e/124 user libc printf"
