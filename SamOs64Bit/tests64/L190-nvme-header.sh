#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

[ -f "$ROOT/src/disk/drivers/nvme.h" ] || fail "nvme.h missing"

for sym in NVME_SECTOR_SIZE NVME_PCI_BASE_CLASS NVME_PCI_SUBCLASS \
           NVME_BASE_REGISTER_CAP NVME_BASE_REGISTER_AQA \
           NVME_SQTDBL_OFFSET NVME_CQTDBL_OFFSET \
           NVME_OPCODE_READ NVME_OPCODE_WRITE \
           NVME_COMMAND_BITS_BUILD \
           'struct nvme_submission_queue_entry' \
           'struct nvme_disk_driver_private' \
           nvme_driver_init; do
    grep -q "$sym" "$ROOT/src/disk/drivers/nvme.h" || fail "nvme.h: $sym missing"
done

# Preserve upstream typo.
grep -q 'struct nvme_compeltion_queue_entry' "$ROOT/src/disk/drivers/nvme.h" \
    || fail "nvme.h: upstream nvme_compeltion_queue_entry typo not preserved"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L190 nvme header"
