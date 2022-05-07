#!/usr/bin/env bash
# e2e/115-multiheap-determinism.sh - two alloc/free/alloc cycles
# at 256 bytes both succeed. Catches multiheap free-list
# corruption under repeated churn.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 159 -eq 1 "consecutive alloc/free/alloc cycles succeed"
echo "PASS: e2e/115 multiheap determinism"
