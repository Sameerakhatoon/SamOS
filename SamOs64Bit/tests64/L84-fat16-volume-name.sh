#!/usr/bin/env bash
# L84 - FAT16 volume_name plumbed end to end; disk.c L78 #if 0 dropped.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)

fail(){ echo "FAIL: $*" >&2; exit 1; }

# file.h gets the typedef and the vtable slot.
grep -q 'FS_VOLUME_NAME_FUNCTION' "$ROOT/src/fs/file.h" \
    || fail "file.h: typedef missing"
grep -q 'volume_name;' "$ROOT/src/fs/file.h" \
    || fail "file.h: vtable slot missing"

# fat16.c implements it and exports through fat16_fs.
grep -q 'fat16_volume_name' "$ROOT/src/fs/fat/fat16.c" \
    || fail "fat16.c: fat16_volume_name missing"
grep -q '.volume_name = fat16_volume_name' "$ROOT/src/fs/fat/fat16.c" \
    || fail "fat16.c: vtable not wired"
grep -q 'char\s\+name\[11\]' "$ROOT/src/fs/fat/fat16.c" \
    || fail "fat16.c: fat_private.name field missing"
grep -q 'volume_id_string' "$ROOT/src/fs/fat/fat16.c" \
    || fail "fat16.c: volume label copy missing"

# disk.c #if 0 is gone and uses volume_name.
if grep -q '^#if 0' "$ROOT/src/disk/disk.c"; then
    fail "disk.c: still has #if 0 guard"
fi
grep -q 'filesystem->volume_name' "$ROOT/src/disk/disk.c" \
    || fail "disk.c: volume_name not called"

# fat16_resolve uses diskstreamer_new_from_disk too.
grep -q 'diskstreamer_new_from_disk(disk)' "$ROOT/src/fs/fat/fat16.c" \
    || fail "fat16.c: resolve must use diskstreamer_new_from_disk"
if grep -q 'diskstreamer_new(disk->id)' "$ROOT/src/fs/fat/fat16.c"; then
    fail "fat16.c: still has id-based streamer construction"
fi

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"

echo "PASS: L84 fat16 volume_name"
