#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# New helpers.
for sym in disk_create_partition disk_filesystem_mount; do
    grep -q "$sym" "$ROOT/src/disk/disk.h" || fail "disk.h: $sym decl missing"
    grep -q "^int $sym" "$ROOT/src/disk/disk.c" || fail "disk.c: $sym body missing"
done

# driver stored on disk_create_new.
grep -q 'disk->driver         = driver' "$ROOT/src/disk/disk.c" \
    || grep -q 'disk->driver = driver' "$ROOT/src/disk/disk.c" \
    || fail "disk.c: driver pointer not stored"

# disk_read_block routes through driver vtable.
grep -q "idisk->driver->functions.read(idisk" "$ROOT/src/disk/disk.c" \
    || fail "disk.c: read does not delegate to driver vtable"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L186 disk rewrite for driver"
