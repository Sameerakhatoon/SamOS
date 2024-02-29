#!/usr/bin/env bash
# e2e/112-api-oob-sentinels.sh - out-of-bound lookups in the
# core kernel APIs return their documented sentinel values.
# Catches off-by-one bugs that would silently return a wild
# pointer instead.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 151 -eq 1 "pci_device_get(99999) returns < 0"
expect_stage_value 152 -eq 1 "e820_entry(99999) returns NULL"
expect_stage_value 153 -eq 1 "disk_get(99) returns NULL"
echo "PASS: e2e/112 OOB sentinels"
