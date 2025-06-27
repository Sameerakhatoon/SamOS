#!/usr/bin/env bash
# e2e/229-net-two-guests.sh - two SamOS guests talk to each other.
# Every other network test pairs one SamOS with the host's side (slirp,
# net_peer.py, host servers); here two QEMUs boot snapshot copies of the
# same image and their virtio-net-pci NICs are joined by a pair of
# -netdev socket UDP endpoints that point at each other. Guest a is
# 10.0.2.15 (52:54:00:12:34:56), guest b 10.0.2.16 (52:54:00:12:34:57),
# set by samos.ip on each one's command line. a runs HTTPD.ELF and
# UDPECHO.ELF -s; once they listen, b boots and runs WGET.ELF for / and
# the 64 KiB /big (length and FNV-1a against the expected bodies),
# UDPECHO.ELF to a, and PING.ELF a. Then a's shell pings b and its ARP
# table shows b's MAC. x86 pc, aarch64 and riscv64 virt. See
# _net_lib.sh, net_two_guests_test.
# e2e-requires: net virtio-pci (a virtio-net-pci NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"
net_two_guests_test pci
echo "PASS: e2e/229 two SamOS guests: ping both ways, TCP (/ and 64 KiB /big) and UDP echo between them"
