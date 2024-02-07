#!/usr/bin/env bash
# e2e/74-pci-host-bridge.sh - PCI scan found a host bridge
# (class 6, subclass 0). QEMU -machine pc always exposes one.
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 93 -eq 1 "PCI host bridge (class 06:00) present"
echo "PASS: e2e/74 pci host bridge"
