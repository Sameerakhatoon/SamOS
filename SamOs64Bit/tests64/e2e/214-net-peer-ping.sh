#!/usr/bin/env bash
# e2e/214-net-peer-ping.sh - SamOS answers pings from the host.
# QEMU's -netdev socket carries the virtio-net-pci NIC's frames to
# net_peer.py, which plays 10.0.2.2: it sees SamOS's gratuitous ARP,
# resolves 10.0.2.15, sends four echo requests and checks the replies
# byte for byte (IP and ICMP checksums, id, sequence, data), and one
# with a bad ICMP checksum that must get no answer. SamOS's own ping
# 10.0.2.2 is answered by the peer the other way.
# e2e-requires: net virtio-pci (a virtio-net-pci NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"
net_peer_test pci
echo "PASS: e2e/214 the host's pings answered over virtio-net-pci, and SamOS pings it"
