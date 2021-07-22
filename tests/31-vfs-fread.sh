#!/bin/bash
# tests/31-vfs-fread.sh
#
# Ch 73 - fread is wired in the VFS layer. With fd=0 (which is always
# invalid because descriptors start at 1) fread short-circuits to
# -EINVARG. -EINVARG is -2; cast to unsigned that is FFFFFFFE.
# kernel_main prints "frd=FFFFFFFE".

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log"

chars=$(cat "$log")

if echo "$chars" | grep -q 'frd=FFFFFFFE'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'frd=FFFFFFFE'"
echo "      first 1000 chars: $(echo "$chars" | head -c 1000)"
exit 1
