#!/usr/bin/env bash
# e2e/103-kzalloc-big.sh - kzalloc(32 KiB) returns memory that
# reads as all zero.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 139 -eq 1 "kzalloc(32 KiB) all bytes zero"
echo "PASS: e2e/103 kzalloc big zero"
