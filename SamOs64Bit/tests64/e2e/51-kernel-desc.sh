#!/usr/bin/env bash
# e2e/51-kernel-desc.sh - L44 kernel_desc() returns the kernel
# paging descriptor.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 66 -eq 1 "kernel_desc() returns non-NULL"
echo "PASS: e2e/51 kernel_desc"
