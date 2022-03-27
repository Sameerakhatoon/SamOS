#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# Makefile entries.
grep -q 'build/disk/drivers/nvme.o' "$ROOT/Makefile" \
    || fail "Makefile: nvme.o missing from FILES"
grep -q '^./build/disk/drivers/nvme.o:' "$ROOT/Makefile" \
    || fail "Makefile: nvme.o recipe missing"

# NVME registered in driver registry.
grep -q 'disk_driver_register(nvme_driver_init())' "$ROOT/src/disk/driver.c" \
    || fail "driver.c: NVME registration missing"

# Upstream L195 fixes applied.
grep -q 'nvme_disk_driver_umount(' "$ROOT/src/disk/drivers/nvme.c" \
    && fail "nvme.c: nvme_disk_driver_umount typo call still present" || true
count=$(grep -c '^        slba += nlb;' "$ROOT/src/disk/drivers/nvme.c")
[ "$count" -ge 2 ] || fail "nvme.c: read+write paths should both use slba += nlb (got $count)"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L195 nvme finalize"
