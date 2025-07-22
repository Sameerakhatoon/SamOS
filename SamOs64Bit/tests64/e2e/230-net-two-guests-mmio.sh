#!/usr/bin/env bash
# e2e/230-net-two-guests-mmio.sh - e2e/229 on virtio-net-device
# (virtio-mmio, QEMU virt): two SamOS guests, each NIC on its MMIO
# slot's line, joined by a pair of -netdev socket UDP endpoints. Guest
# a 10.0.2.15 serves HTTPD.ELF and UDPECHO.ELF -s; b 10.0.2.16 fetches
# / and the 64 KiB /big, sends a datagram and pings a; then a's shell
# pings b. aarch64 and riscv64 (x86 pc and q35 have no virtio-mmio).
# See _net_lib.sh, net_two_guests_test.
# e2e-requires: net virtio-mmio (a virtio-net-device NIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"
net_two_guests_test mmio
echo "PASS: e2e/230 two SamOS guests on virtio-net-device: ping both ways, TCP (/ and 64 KiB /big) and UDP echo between them"
