#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

[ -f "$ROOT/src/disk/drivers/pata.c" ] || fail "pata.c missing"

for sym in pata_pci_device_present pata_private_new pata_disk_base_drive_address \
           pata_disk_ctrl_drive_address pata_disk_read_sector \
           pata_driver_read pata_driver_write pata_driver_mount \
           pata_driver_unmount pata_driver_loaded pata_driver_unloaded \
           pata_driver_mount_partition pata_driver pata_driver_init; do
    grep -q "$sym" "$ROOT/src/disk/drivers/pata.c" || fail "pata.c: $sym missing"
done

# disk_private_data_driver accessor.
grep -q '^void\* disk_private_data_driver' "$ROOT/src/disk/disk.c" \
    || fail "disk.c: disk_private_data_driver body missing"
grep -q 'disk_private_data_driver' "$ROOT/src/disk/disk.h" \
    || fail "disk.h: disk_private_data_driver decl missing"

# Driver registered.
grep -q 'disk_driver_register(pata_driver_init())' "$ROOT/src/disk/driver.c" \
    || fail "driver.c: PATA registration missing"

# Makefile recipe.
grep -q 'build/disk/drivers/pata.o' "$ROOT/Makefile" || fail "Makefile: pata.o missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L188 pata source"
