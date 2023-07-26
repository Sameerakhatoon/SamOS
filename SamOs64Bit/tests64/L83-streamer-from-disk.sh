#!/usr/bin/env bash
# L83 - diskstreamer_new_from_disk and FAT16 uses it.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)

fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'diskstreamer_new_from_disk' "$ROOT/src/disk/streamer.h" \
    || fail "streamer.h: prototype missing"
grep -q 'diskstreamer_new_from_disk(struct disk\* disk)' "$ROOT/src/disk/streamer.c" \
    || fail "streamer.c: definition missing"

# FAT16 init_private must use the new constructor for all three streams.
n=$(grep -c 'diskstreamer_new_from_disk(disk)' "$ROOT/src/fs/fat/fat16.c")
[ "$n" -ge 3 ] || fail "fat16.c: expected 3 diskstreamer_new_from_disk(disk) calls, found $n"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"

echo "PASS: L83 streamer_from_disk"
