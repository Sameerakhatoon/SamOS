#!/usr/bin/env bash
# e2e/25-krealloc.sh - krealloc round trip preserves prefix bytes.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_value 31 -eq 1 "krealloc(p, 256) preserves first 64 bytes"
echo "PASS: e2e/25 krealloc"
