#!/bin/bash
# tests/33-fstat.sh
#
# Ch 78+79 - VFS fstat dispatches to fat16_stat, which reads the
# size and attribute byte off the cached fat_directory_item. hello.txt
# is 13 bytes ("Hello world!\n") with no read-only attribute, so the
# kernel prints "fz=0000000D ffl=00000000".

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log"

chars=$(cat "$log")

ok=1
echo "$chars" | grep -q 'fz=0000000D'  || { echo "FAIL: filesize != 13"; ok=0; }
echo "$chars" | grep -q 'ffl=00000000' || { echo "FAIL: flags != 0"; ok=0; }

if [ $ok -ne 1 ]; then
    echo "      first 1200 chars: $(echo "$chars" | head -c 1200)"
    exit 1
fi
exit 0
