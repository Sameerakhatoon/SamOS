#!/usr/bin/env bash
# e2e/57-kmalloc-align.sh - kmalloc(8) returns an 8-byte aligned
# pointer. Sanity check on the multiheap block header layout.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 73 -eq 1 "kmalloc(8) is 8-byte aligned"
echo "PASS: e2e/57 kmalloc alignment"
