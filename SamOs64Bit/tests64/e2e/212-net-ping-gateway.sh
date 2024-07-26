#!/usr/bin/env bash
# e2e/212-net-ping-gateway.sh - virtio-net-pci on QEMU's user
# network. SamOS comes up as 10.0.2.15/24 with the default route via
# 10.0.2.2; the shell's ifconfig, route and arp print SamTcpIp's
# tables, and ping 10.0.2.2 gets echo replies from slirp (the first
# request goes to ARP resolution, SamTcpIp drops it on the ARP miss).
# x86: pc and q35. aarch64 and riscv64: virt, MSI-X through the ITS or
# the PCI INTx line.
# e2e-requires: net virtio-pci (a virtio-net-pci NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"

if [ "$E2E_ARCH" = x86_64 ]; then
    for m in pc q35; do
        E2E_MACHINE=$m
        net_gateway_test pci "$m/pci"
        cp "$E2E_SERIAL" "$E2E_TMP/serial-$m.log"
    done
    expect_serial '^virtio-net: eth0 .*, pci, MSI-X vector [0-9]+,' "q35: MSI-X"
else
    net_gateway_test pci
fi
echo "PASS: e2e/212 virtio-net-pci, ifconfig, route, arp, ping 10.0.2.2 on -netdev user"
