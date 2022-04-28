#!/usr/bin/env bash
# e2e/88-kmalloc-stress.sh - L34 multiheap stress: 100 small
# allocations back to back, then free them all.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 118 -eq 1 "100 small kmalloc back-to-back all non-NULL"
echo "PASS: e2e/88 kmalloc stress"
