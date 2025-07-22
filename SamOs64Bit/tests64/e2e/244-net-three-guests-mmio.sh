#!/usr/bin/env bash
# e2e/244-net-three-guests-mmio.sh - e2e/231 on virtio-net-device
# (virtio-mmio, QEMU virt). The same three guests on the same hub
# (net_host.py's learning switch): two concurrent 4 MiB fetches from
# a, UDP echo between every pair, every ARP table complete. 231 and
# 233 to 236 take E2E_NET=mmio from the environment (net_transport in
# _net_lib.sh); this runs 231 with it. aarch64 and riscv64.
# e2e-requires: net virtio-mmio (a virtio-net-device NIC)
E2E_NET=mmio exec bash "$(dirname "$0")/231-net-three-guests.sh"
