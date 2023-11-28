#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

[ -f "$ROOT/src/disk/drivers/pata.h" ] || fail "pata.h missing"

for sym in KERNEL_PATA_SECTOR_SIZE PATA_PCI_BASE_CLASS PATA_PCI_SUBCLASS \
           PATA_PRIMARY_DRIVE_BASE_ADDRESS PATA_PRIMARY_DRIVE \
           PATA_SECONDARY_DRIVE PATA_PARTITION_DRIVE \
           PATA_DISK_DRIVE 'struct pata_driver_private_data' \
           pata_driver_init; do
    grep -q "$sym" "$ROOT/src/disk/drivers/pata.h" || fail "pata.h: $sym missing"
done

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L187 pata header"
