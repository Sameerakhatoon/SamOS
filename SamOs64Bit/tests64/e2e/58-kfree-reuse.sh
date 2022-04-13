#!/usr/bin/env bash
# e2e/58-kfree-reuse.sh - kmalloc -> kfree -> kmalloc again
# returns non-NULL (does NOT assert same address; the multiheap
# is free to pick from a different free-list slot).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 74 -eq 1 "kmalloc / kfree / kmalloc returns non-NULL"
echo "PASS: e2e/58 kfree round trip"
