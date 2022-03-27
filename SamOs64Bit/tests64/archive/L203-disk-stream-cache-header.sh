#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# disk.h forward decl + cache field.
grep -q 'struct disk_stream_cache;' "$ROOT/src/disk/disk.h" \
    || fail "disk.h: forward decl missing"
grep -q 'struct disk_stream_cache\* cache' "$ROOT/src/disk/disk.h" \
    || fail "disk.h: cache field missing"

# streamer.h types.
for sym in DISK_STREAMER_CACHE_STATUS_NEW_CACHE_ENTRY \
           DISK_STREAMER_MAX_CACHE_SECTOR_SIZE \
           DISK_STREAM_LEVEL3_SECTORS_ARRAY_SIZE \
           DISK_STREAM_BUCKET_ARRAY_SIZE \
           DISK_STREAM_CACHE_ROUNDROBIN_MAX \
           DISK_STREAM_BUCKET3_BYTE_SIZE \
           'struct disk_stream_cache_sector' \
           'struct disk_stream_cache_bucket_level3' \
           'struct disk_stream_cache_bucket_level2' \
           'struct disk_stream_cache_bucket_level1' \
           'struct disk_stream_cache_round_robin' \
           'struct disk_stream_cache'; do
    grep -q "$sym" "$ROOT/src/disk/streamer.h" \
        || fail "streamer.h: $sym missing"
done

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L203 disk stream cache header"
