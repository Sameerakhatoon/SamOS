#!/usr/bin/env bash
# e2e/215-net-peer-ping-mmio.sh - e2e/214 on virtio-net-device.
# e2e-requires: net virtio-mmio (a virtio-net-device NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"
net_peer_test mmio
echo "PASS: e2e/215 the host's pings answered over virtio-net-device, and SamOS pings it"
