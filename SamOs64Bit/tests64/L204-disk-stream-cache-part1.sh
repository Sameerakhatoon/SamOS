#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

for sym in diskstreamer_cache_new diskstreamer_cache_bucket_level1_get \
           diskstreamer_cache_bucket_level2_get \
           diskstreamer_cache_bucket_level3_get \
           diskstreamer_cache_bucket_level3_free; do
    grep -q "$sym" "$ROOT/src/disk/streamer.c" || fail "streamer.c: $sym missing"
done

# Preserve upstream `disk_Stream_cache_bucket_level1` typo verbatim.
grep -q 'disk_Stream_cache_bucket_level1' "$ROOT/src/disk/streamer.c" \
    || fail "streamer.c: upstream typo not preserved"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L204 disk stream cache part 1"
