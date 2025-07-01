#!/usr/bin/env bash
# e2e/232-net-router.sh - a SamOS guest routes between two subnets.
# Three QEMUs boot snapshot copies of one image. Guest b has two
# virtio-net-pci NICs and samos.ip_forward; each of its NICs is joined to
# one other guest by its own pair of -netdev socket UDP endpoints:
#
#   a 10.1.0.2 --- 10.1.0.1 eth0 [b] eth1 10.2.0.1 --- 10.2.0.2 c
#
# a's default route goes via b (samos.gw=10.1.0.1); c has no default
# route, only samos.route=10.1.0.0/24@10.2.0.1. c runs HTTPD.ELF and
# UDPECHO.ELF -s; a fetches / and the 64 KiB /big with WGET.ELF (length
# and FNV-1a), sends a datagram to c, pings c with PING.ELF (the replies
# arrive with TTL 63: b took one off) and once with TTL 1 (b answers
# with Time Exceeded). Then c's shell pings a and sends a datagram to
# a's UDPECHO.ELF -s. b's route shows the forwarding counters, its
# ifconfig both NICs' counters, its ARP table one neighbour per NIC;
# a's ARP table has b and not c. x86 pc, aarch64 and riscv64 virt.
# e2e-requires: net virtio-pci (two virtio-net-pci NICs on the router)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"
net_router_test pci
echo "PASS: e2e/232 SamOS routes between two subnets: ping both ways with TTL 63, Time Exceeded at TTL 1, TCP (/ and 64 KiB /big) and UDP echo both ways through the router"
