#!/usr/bin/env bash
# e2e/15-fs-fopen.sh - VFS open by path on the FAT16 SAMOS partition.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_value 19 -eq 1 "fopen(\"@:/BLANK.ELF\") returns fd > 0"
echo "PASS: e2e/15 fs fopen"
