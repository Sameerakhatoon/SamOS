#!/usr/bin/env bash
# e2e/216-net-irq-paths.sh - e2e/212's ping on each of the
# virtio-net driver's interrupt paths: samos.net_poll (no interrupt, the
# BSP's tick polls), samos.net_intx (the PCI INTx line instead of
# MSI-X), and the MSI-X default where 212 may not see it: x86 q35 is in
# 212; riscv64 virt with the APLIC and IMSIC (aia=aplic-imsic, no MSI
# with the PLIC); aarch64 with its=off falls back to INTx.
# e2e-requires: net virtio-pci (a virtio-net-pci NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"

image=$E2E_IMAGE
net_cmdline samos.net_poll
NET_IRQ=polled net_gateway_test pci "polled"
E2E_IMAGE=$image
net_cmdline samos.net_intx
net_gateway_test pci "intx"
expect_serial '^virtio-net: eth0 .*, pci, line, GSI [0-9]+,' "intx: the INTx line"
E2E_IMAGE=$image
case $E2E_ARCH in
    riscv64)
        E2E_RISCV64_MACHINE_OPTS=,aia=aplic-imsic net_gateway_test pci "imsic"
        expect_serial '^virtio-net: eth0 .*, pci, MSI-X vector [0-9]+,' "imsic: MSI-X through the IMSIC" ;;
    aarch64)
        E2E_AARCH64_MACHINE_OPTS=,its=off net_gateway_test pci "its=off"
        expect_serial '^virtio-net: eth0 .*, pci, line, GSI [0-9]+,' "its=off: INTx without an ITS" ;;
esac
echo "PASS: e2e/216 ping on the polled, INTx and MSI-X paths"
