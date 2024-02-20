#!/usr/bin/env bash
# e2e/107-kfree-null.sh - kfree(NULL) is a no-op (does not crash).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 143 -eq 1 "kfree(NULL) no-op"
echo "PASS: e2e/107 kfree(NULL)"
