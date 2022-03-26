#!/usr/bin/env bash
# e2e/18-pci-classes.sh - PCI enumeration produced a list that
# includes the expected QEMU IDE controller (class 0x01:0x01)
# and a VGA-class device (class 0x03).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 20
expect_stage_value 23 -eq 1 "PCI list includes IDE controller (0x01:0x01)"
expect_stage_value 24 -eq 1 "PCI list includes VGA device (class 0x03)"
echo "PASS: e2e/18 pci classes"
