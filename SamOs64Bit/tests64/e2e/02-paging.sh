#!/usr/bin/env bash
# e2e/02-paging.sh - kernel built its own PML4 and switched CR3.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 15
expect_stage_reached 1 "kernel_main"
expect_stage_reached 2 "paging switched to kernel desc"
echo "PASS: e2e/02 paging"
