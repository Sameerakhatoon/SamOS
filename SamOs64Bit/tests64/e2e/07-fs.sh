#!/usr/bin/env bash
# e2e/07-fs.sh - fs_init wired the FAT16 driver in the VFS.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 15
expect_stage_reached 7 "fs_init returned"
echo "PASS: e2e/07 fs"
