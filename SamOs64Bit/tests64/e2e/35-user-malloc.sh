#!/usr/bin/env bash
# e2e/35-user-malloc.sh - userland malloc + free round trip.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 45 -eq 1 "user malloc(128) write+read+free round trip"
echo "PASS: e2e/35 user malloc"
