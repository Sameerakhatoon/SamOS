#!/bin/bash
# tests/34-fclose.sh
#
# Ch 80+81 - fclose dispatches into fat16_close, which frees the
# fat_item chain + the fat_file_descriptor; then the VFS layer clears
# file_descriptors[fd-1] and kfrees the wrapping file_descriptor.
# kernel_main prints "afterclose=ok" after the call; if anything in
# the free chain double-frees or scrolls VGA, the marker doesn't make
# it onto the screen.

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log"

chars=$(cat "$log")

if echo "$chars" | grep -q 'afterclose=ok'; then
    exit 0
fi

echo "FAIL: VGA buffer did not contain 'afterclose=ok'"
echo "      first 1500 chars: $(echo "$chars" | head -c 1500)"
exit 1
