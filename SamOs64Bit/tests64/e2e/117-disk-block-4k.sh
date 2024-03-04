#!/usr/bin/env bash
# e2e/117-disk-block-4k.sh - disk_read_block at LBA 8 succeeds.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 161 -eq 1 "disk_read_block(d, 8, 1, buf) == 0"
echo "PASS: e2e/117 disk LBA 8"
