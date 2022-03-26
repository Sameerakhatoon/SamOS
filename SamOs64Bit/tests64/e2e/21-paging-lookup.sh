#!/usr/bin/env bash
# e2e/21-paging-lookup.sh - paging_get_physical_address recovers
# the identity-mapped kernel-load address.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_value 27 -eq 1 "paging_get_physical_address(0x100000) == 0x100000"
echo "PASS: e2e/21 paging lookup"
