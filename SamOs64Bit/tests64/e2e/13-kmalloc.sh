#!/usr/bin/env bash
# e2e/13-kmalloc.sh - kernel heap exercise.
#
# kernel_selftest runs three heap probes:
#   BM_FEATURE_KMALLOC_RT  (16) - kmalloc(1024) write + verify + free
#   BM_FEATURE_KMALLOC_BIG (17) - kmalloc(64 KiB) returns non-NULL
#   BM_FEATURE_KZALLOC_ZERO(28) - kzalloc(256) reads as zero
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_value 16 -eq 1 "kmalloc round trip"
expect_stage_value 17 -eq 1 "kmalloc 64 KiB"
expect_stage_value 28 -eq 1 "kzalloc returns zeroed memory"
echo "PASS: e2e/13 kmalloc"
