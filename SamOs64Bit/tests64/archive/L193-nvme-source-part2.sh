#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

for sym in nvme_admin_submission_queue_init nvme_admin_completion_queue_init \
           nvme_cap_doorbell_stride nvme_admin_send_command \
           nvme_pci_mmio_base nvme_disk_driver_mount_for_device; do
    grep -q "$sym" "$ROOT/src/disk/drivers/nvme.c" || fail "nvme.c: $sym missing"
done

# Upstream typos preserved.
grep -q 'nvme_disk_driver_umount' "$ROOT/src/disk/drivers/nvme.c" \
    || fail "nvme.c: upstream nvme_disk_driver_umount typo not preserved"
grep -q '0xFFFFFFFF0ULL' "$ROOT/src/disk/drivers/nvme.c" \
    || fail "nvme.c: upstream BAR low-mask typo not preserved"

# pci.h public decl renamed.
grep -q 'void pci_enable_bus_master(struct pci_device' "$ROOT/src/io/pci.h" \
    || fail "pci.h: pci_enable_bus_master rename missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L193 nvme source part 2"
