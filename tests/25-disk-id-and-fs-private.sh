#!/bin/bash
# tests/25-disk-id-and-fs-private.sh
#
# Ch 67 - disk_search_and_init now zero-initialises disk.id and leaves
# disk.fs_private null (FAT16's resolver will fill it in later).
# kernel_main prints both as hex; expect "did=00000000 priv=00000000".

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log"

chars=$(cat "$log")

if echo "$chars" | grep -q 'did=00000000'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'did=00000000'"
echo "      first 800 chars: $(echo "$chars" | head -c 800)"
exit 1
