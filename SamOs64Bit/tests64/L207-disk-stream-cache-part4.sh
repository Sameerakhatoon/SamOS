#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

# diskstreamer_new now forwards to diskstreamer_new_from_disk.
awk '/^struct disk_stream\* diskstreamer_new\(int/,/^}$/' \
    "$ROOT/src/disk/streamer.c" | grep -q 'return diskstreamer_new_from_disk(disk)' \
    || fail "streamer.c: diskstreamer_new does not delegate"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L207 disk stream cache part 4"
