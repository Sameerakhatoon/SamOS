#!/usr/bin/env bash
# e2e/43-gpt-virtual-disks.sh - L77/L78 GPT partition virtual
# disks. disk_get(1) should be non-NULL when the GPT contains
# at least one user partition (UEFI image has p1 + p2).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 57 -eq 1 "disk_get(1) returns a GPT virtual disk"
echo "PASS: e2e/43 gpt virtual disks"
