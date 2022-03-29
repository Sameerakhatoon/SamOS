#!/usr/bin/env bash
# e2e/23-cpuid.sh - CPUID leaf 0 returns a non-zero vendor signature.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_value 29 -eq 1 "cpuid leaf 0 returns non-zero vendor signature"
echo "PASS: e2e/23 cpuid"
