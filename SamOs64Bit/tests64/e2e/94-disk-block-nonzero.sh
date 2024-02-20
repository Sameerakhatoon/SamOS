#!/usr/bin/env bash
# e2e/94-disk-block-nonzero.sh - L40 disk_read_block at LBA 2
# (not the boot sector) returns 0. Catches geometry regressions
# that would only fail past sector 0.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 126 -eq 1 "disk_read_block(d, 2, 1, buf) == 0"
echo "PASS: e2e/94 disk block 2"
