#!/usr/bin/env bash
# e2e/89-kmalloc-big-recycle.sh - 1 MiB alloc + free + 1 MiB alloc
# recycles the multiheap free list cleanly.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 119 -eq 1 "1 MiB alloc + free + 1 MiB alloc returns non-NULL"
echo "PASS: e2e/89 kmalloc big recycle"
