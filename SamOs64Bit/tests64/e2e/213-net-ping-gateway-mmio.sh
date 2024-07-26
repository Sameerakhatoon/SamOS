#!/usr/bin/env bash
# e2e/213-net-ping-gateway-mmio.sh - e2e/212 on virtio-net-device
# (virtio-mmio, QEMU virt): the NIC's interrupt is its slot's line.
# e2e-requires: net virtio-mmio (a virtio-net-device NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"
net_gateway_test mmio
echo "PASS: e2e/213 virtio-net-device, ifconfig, route, arp, ping 10.0.2.2 on -netdev user"
