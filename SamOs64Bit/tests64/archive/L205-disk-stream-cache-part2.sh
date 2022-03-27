#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

for sym in diskstreamer_cache_round_robin_add diskstreamer_cache_find; do
    grep -q "$sym" "$ROOT/src/disk/streamer.c" || fail "streamer.c: $sym missing"
done

# struct disk_stream gains sector_size.
grep -q '    int           sector_size;' "$ROOT/src/disk/streamer.h" \
    || fail "streamer.h: sector_size field missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L205 disk stream cache part 2"
