#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# struct disk gains hardware_disk + driver_private.
grep -q 'struct disk\*.*hardware_disk' "$ROOT/src/disk/disk.h" \
    || fail "disk.h: hardware_disk field missing"
grep -q 'void\*.*driver_private' "$ROOT/src/disk/disk.h" \
    || fail "disk.h: driver_private field missing"

# disk_hardware_disk accessor.
grep -q 'disk_hardware_disk' "$ROOT/src/disk/disk.h" \
    || fail "disk.h: disk_hardware_disk decl missing"
grep -q '^struct disk\* disk_hardware_disk' "$ROOT/src/disk/disk.c" \
    || fail "disk.c: disk_hardware_disk body missing"

# disk_create_new takes the widened signature.
grep -q 'disk_create_new(struct disk_driver\* driver' "$ROOT/src/disk/disk.h" \
    || fail "disk.h: widened disk_create_new sig missing"

# REAL-disk and partition-disk validations.
grep -q 'type == SAMOS_DISK_TYPE_REAL' "$ROOT/src/disk/disk.c" \
    || fail "disk.c: REAL-disk validation missing"
grep -q 'hardware_disk->type != SAMOS_DISK_TYPE_REAL' "$ROOT/src/disk/disk.c" \
    || fail "disk.c: hardware-disk type check missing"

# Callers updated.
grep -q 'disk_create_new(NULL, NULL, SAMOS_DISK_TYPE_REAL' "$ROOT/src/disk/disk.c" \
    || fail "disk.c: search_and_init call not updated"
grep -q 'disk_create_new(NULL, gpt_primary_disk' "$ROOT/src/disk/gpt.c" \
    || fail "gpt.c: partition create call not updated"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L183 disk hardware attach"
