#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# disk_search_and_init returns int.
grep -q '^int          disk_search_and_init' "$ROOT/src/disk/disk.h" \
    || fail "disk.h: disk_search_and_init must return int"
grep -q '^int disk_search_and_init' "$ROOT/src/disk/disk.c" \
    || fail "disk.c: disk_search_and_init must return int"

# disk_mount_all helper.
grep -q '^int disk_mount_all' "$ROOT/src/disk/disk.c" \
    || fail "disk.c: disk_mount_all missing"

# Boot path now drives the driver registry.
grep -q 'disk_driver_system_init()' "$ROOT/src/disk/disk.c" \
    || fail "disk.c: disk_driver_system_init not called"

# disk_driver_register fix.
grep -q 'if(disk_driver_registered(driver))' "$ROOT/src/disk/driver.c" \
    || fail "driver.c: disk_driver_registered self-recursion fix missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L189 pata finalize"
