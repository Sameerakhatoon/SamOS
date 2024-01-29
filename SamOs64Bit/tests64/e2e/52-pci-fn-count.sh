#!/usr/bin/env bash
# e2e/52-pci-fn-count.sh - PCI device count > 1 (QEMU PC chipset
# exposes several distinct devices: host bridge, IDE, VGA, ...).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 68 -eq 1 "pci_device_count > 1"
echo "PASS: e2e/52 pci fn count"
