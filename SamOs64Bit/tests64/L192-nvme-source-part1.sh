#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

[ -f "$ROOT/src/disk/drivers/nvme.c" ] || fail "nvme.c missing"

for sym in nvme_read32 nvme_write32 nvme_read64 nvme_write64 \
           nvme_admin_cmd_raw nvme_create_io_cq nvme_create_io_sq \
           nvme_pci_private_new nvme_pci_device \
           nvme_disk_driver_read_reg nvme_disk_driver_write_reg \
           nvme_map_mmio_once; do
    grep -q "$sym" "$ROOT/src/disk/drivers/nvme.c" || fail "nvme.c: $sym missing"
done

# L192 fixes the completion-queue struct name.
grep -q '^struct nvme_completion_queue_entry {' "$ROOT/src/disk/drivers/nvme.h" \
    || fail "nvme.h: struct rename to nvme_completion_queue_entry missing"
# L192 fixes the typo in code; comments may still reference it.
grep -E '^\s*struct\s+nvme_compeltion_queue_entry\s*\{' "$ROOT/src/disk/drivers/nvme.h" \
    && fail "nvme.h: stale nvme_compeltion typo present in struct def" || true

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L192 nvme source part 1"
