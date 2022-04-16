#!/usr/bin/env bash
# e2e/68-kmalloc-1mb.sh - L34 multiheap large allocation.
# Verifies the heap has enough headroom and the allocator can
# coalesce free pages to satisfy a 1 MiB request.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 87 -eq 1 "kmalloc(1 MiB) returns non-NULL"
echo "PASS: e2e/68 kmalloc 1 MiB"
