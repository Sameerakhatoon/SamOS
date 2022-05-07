#!/usr/bin/env bash
# e2e/111-kfree-single.sh - the kfree API surface is reachable
# without crashing on a single freed pointer.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 150 -eq 1 "kfree single-call path safe"
echo "PASS: e2e/111 kfree single"
