#!/usr/bin/env bash
# e2e/245-net-router-mmio.sh - e2e/232 on virtio-net-device
# (virtio-mmio, QEMU virt). Guest b routes between two subnets with two
# virtio-net-device NICs (eth0 is the first -device: the kernel probes
# the MMIO slots from the highest address down, where QEMU virt puts
# the first device); a and c each have one:
#
#   a 10.1.0.2 --- 10.1.0.1 eth0 [b] eth1 10.2.0.1 --- 10.2.0.2 c
#
# Ping both ways (TTL 63 through b), Time Exceeded at TTL 1, TCP and
# UDP through the router, b's counters and per-interface ARP. aarch64
# and riscv64.
# e2e-requires: net virtio-mmio (two virtio-net-device NICs on the router)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_net_lib.sh"
net_router_test mmio
echo "PASS: e2e/245 SamOS routes between two subnets on virtio-net-device: ping both ways with TTL 63, Time Exceeded at TTL 1, TCP (/ and 64 KiB /big) and UDP echo both ways through the router"
