#!/usr/bin/env bash
# e2e/97-krealloc-shrink.sh - L80 krealloc to a smaller size
# still returns a valid pointer.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 129 -eq 1 "krealloc(p, 1024 -> 64) returns non-NULL"
echo "PASS: e2e/97 krealloc shrink"
