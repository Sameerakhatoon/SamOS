#!/usr/bin/env bash
# e2e/77-nvme-present.sh - L86-L91 NVMe driver enumerates a
# QEMU-attached NVMe controller via PCI class 0x01 subclass 0x08.
#
# QEMU's -machine pc does not include an NVMe controller by
# default; we attach one via -device nvme,... and assert the
# kernel's PCI scan saw class 0x01:0x08. The backing image is
# tiny (4 MiB) and lives at /tmp/nvme.img so we do not pull a
# real disk into the test.
. "$(dirname "$0")/_lib.sh"

# Pre-create the backing image if it is missing.
if [ ! -f /tmp/nvme.img ]; then
    dd if=/dev/zero of=/tmp/nvme.img bs=1M count=4 status=none
fi

# QEMU 8.x style: drive id linked to nvme device.
export E2E_EXTRA_QEMU_ARGS="-drive file=/tmp/nvme.img,if=none,id=nvm,format=raw -device nvme,drive=nvm,serial=samos-e2e"

boot_and_dump 25

# Re-scan the PCI device list for class 0x01:0x08 (NVMe).  The
# slot we use is the new BM_FEATURE_NVMe_PRESENT (96), which
# kernel_selftest sets from a dedicated probe.
expect_stage_value 96 -eq 1 "PCI list contains NVMe controller (0x01:0x08)"
echo "PASS: e2e/77 nvme present"
