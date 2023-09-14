#!/usr/bin/env bash
# e2e/125-user-libc-strings.sh - userland atoi/strtol (with
# ERANGE), strchr/strstr/strcat, calloc overflow -> ENOMEM, itoa_r,
# and the quoting parser, run inside SamOS by blank.elf. Value 1 =
# all passed; 0x100+N = check N in libc_string_checks failed.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 169 -eq 1 "user libc strings/numbers/errno/parser"
echo "PASS: e2e/125 user libc strings"
