#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

[ -f "$ROOT/src/disk/driver.h" ] || fail "driver.h missing"
[ -f "$ROOT/src/disk/driver.c" ] || fail "driver.c missing"

for sym in DISK_DRIVER_NAME_SIZE 'struct disk_driver' DISK_DRIVER_LOADED \
           DISK_DRIVER_MOUNT DISK_DRIVER_READ DISK_DRIVER_WRITE \
           DISK_DRIVER_MOUNT_PARTITION \
           disk_driver_register disk_driver_registered \
           disk_driver_mount_all disk_driver_mount_partition \
           disk_driver_system_init disk_driver_system_load_drivers; do
    grep -q "$sym" "$ROOT/src/disk/driver.h" || fail "driver.h: $sym missing"
done

# disk.h gets the driver field + include.
grep -q '#include "driver.h"' "$ROOT/src/disk/disk.h" \
    || fail "disk.h: driver.h not included"
grep -q 'struct disk_driver\* driver' "$ROOT/src/disk/disk.h" \
    || fail "disk.h: driver field missing"

# Preserve upstream typo: disk_driver_register calls itself.
grep -q 'if(disk_driver_register(driver))' "$ROOT/src/disk/driver.c" \
    || fail "driver.c: upstream self-recursion typo not preserved"

# Makefile recipe.
grep -q 'build/disk/driver.o' "$ROOT/Makefile" || fail "Makefile: driver.o missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L184 disk driver"
