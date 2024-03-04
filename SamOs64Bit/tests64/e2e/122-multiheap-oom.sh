#!/usr/bin/env bash
# e2e/122-multiheap-oom.sh - kmalloc(SIZE_MAX) returns NULL
# gracefully (no panic on impossible allocation).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 166 -eq 1 "kmalloc(SIZE_MAX) returns NULL"
echo "PASS: e2e/122 multiheap OOM"
