#!/usr/bin/env bash
# e2e/06-disk.sh - disk_search_and_init found the boot disk and
# fs_resolve picked it as the primary filesystem disk. Marker
# value is (primary_fs_disk->id + 1) so 0 = none found.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_reached 6 "disk_search_and_init returned"
expect_stage_value   6 -gt 0 "primary_fs_disk resolved"
echo "PASS: e2e/06 disk"
