#!/usr/bin/env bash
# e2e/29-disk-enum.sh - disk_get(0) returns a real disk pointer.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_value 36 -eq 1 "disk_get(0) returns non-NULL"
echo "PASS: e2e/29 disk enum"
