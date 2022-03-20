#!/usr/bin/env bash
# e2e/05-pci.sh - pci_init enumerated at least one device.
# Under QEMU `-machine pc` we expect ~5: host bridge, ISA bridge,
# IDE controller, VGA, etc.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 15
expect_stage_reached 4 "IDT live"
expect_stage_reached 5 "PCI scanned"
expect_stage_value   5 -gt 0 "PCI device count > 0"
echo "PASS: e2e/05 pci"
