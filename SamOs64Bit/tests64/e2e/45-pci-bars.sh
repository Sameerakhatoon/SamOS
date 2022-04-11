#!/usr/bin/env bash
# e2e/45-pci-bars.sh - L181 PCI scan populated at least one device
# with a non-zero BAR address or size.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 60 -eq 1 "at least one PCI device has a non-zero BAR"
echo "PASS: e2e/45 pci bars"
