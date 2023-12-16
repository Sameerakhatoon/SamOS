#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

for sym in disk_real_sector disk_real_offset; do
    grep -q "$sym" "$ROOT/src/disk/disk.h" || fail "disk.h: $sym decl missing"
    grep -q "^long $sym" "$ROOT/src/disk/disk.c" || fail "disk.c: $sym body missing"
done

# disk_create_new now allocates the cache.
grep -q 'disk->cache.*= diskstreamer_cache_new' "$ROOT/src/disk/disk.c" \
    || fail "disk.c: cache allocation missing"

# Read body uses disk_real_offset + cache_find.
grep -q 'disk_real_offset(stream->disk' "$ROOT/src/disk/streamer.c" \
    || fail "streamer.c: read body not cached"
grep -q 'diskstreamer_cache_find(stream->disk' "$ROOT/src/disk/streamer.c" \
    || fail "streamer.c: read body missing cache_find"

# Streamer constructors init sector_size.
grep -q 'streamer->sector_size = disk->sector_size' "$ROOT/src/disk/streamer.c" \
    || fail "streamer.c: sector_size init missing"

# diskstreamer_cache_new exposed.
grep -q 'diskstreamer_cache_new()' "$ROOT/src/disk/streamer.h" \
    || fail "streamer.h: cache_new decl missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L206 disk stream cache part 3"
