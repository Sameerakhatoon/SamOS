#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

for sym in nvme_io_submit_and_poll nvme_disk_driver_mount nvme_disk_driver_unmount \
           nvme_disk_driver_read nvme_disk_driver_write \
           nvme_disk_driver_mount_partition nvme_driver nvme_driver_init; do
    grep -q "$sym" "$ROOT/src/disk/drivers/nvme.c" || fail "nvme.c: $sym missing"
done

# Preserve upstream `slba + nlb;` dead expression (now silenced).
grep -q '(slba + nlb)' "$ROOT/src/disk/drivers/nvme.c" \
    || fail "nvme.c: upstream slba + nlb dead expr not preserved"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L194 nvme source part 3"
