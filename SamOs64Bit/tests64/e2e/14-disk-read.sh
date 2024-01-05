#!/usr/bin/env bash
# e2e/14-disk-read.sh - read sector 0 from the primary disk and
# verify the FAT boot signature (0x55 0xAA at offset 510).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_value 18 -eq 1 "disk_read_block(0) returns FAT boot signature"
echo "PASS: e2e/14 disk read"
