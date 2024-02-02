#!/usr/bin/env bash
# e2e/70-disk-sector-512.sh - primary disk sector size == 512.
# Locks down the PATA / GPT virtual-disk geometry.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 89 -eq 1 "primary disk sector_size == 512"
echo "PASS: e2e/70 disk sector 512"
