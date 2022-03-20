#!/usr/bin/env bash
# e2e/03-multiheap.sh - kheap_post_paging completed.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 15
expect_stage_reached 2 "paging"
expect_stage_reached 3 "multiheap ready"
echo "PASS: e2e/03 multiheap"
