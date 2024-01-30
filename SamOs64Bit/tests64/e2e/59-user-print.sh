#!/usr/bin/env bash
# e2e/59-user-print.sh - L101 print() trampoline did not fault.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 81 -eq 1 "print(\"e2e: hello\") round trip"
echo "PASS: e2e/59 user print"
