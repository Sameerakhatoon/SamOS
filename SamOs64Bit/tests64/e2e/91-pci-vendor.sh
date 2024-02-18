#!/usr/bin/env bash
# e2e/91-pci-vendor.sh - PCI scan populated a vendor field that
# is not 0x0000 / 0xFFFF (the no-device sentinels).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 121 -eq 1 "at least one PCI device has a valid vendor"
echo "PASS: e2e/91 pci vendor"
