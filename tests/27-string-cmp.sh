#!/bin/bash
# tests/27-string-cmp.sh
#
# Ch 69 - tolower, strncmp, istrncmp, strnlen_terminator must all work.
# kernel_main prints three probes:
#   istr=00000001  -> istrncmp("AbC","abc",3) == 0 (case insensitive)
#   scmp=00000001  -> strncmp("abc","abd",3) != 0 (last char differs)
#   sterm=00000003 -> strnlen_terminator("foo bar",100,' ') == 3

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log"

chars=$(cat "$log")

ok=1
echo "$chars" | grep -q 'istr=00000001'  || { echo "FAIL: istrncmp"; ok=0; }
echo "$chars" | grep -q 'scmp=00000001'  || { echo "FAIL: strncmp"; ok=0; }
echo "$chars" | grep -q 'sterm=00000003' || { echo "FAIL: strnlen_terminator"; ok=0; }

if [ $ok -ne 1 ]; then
    echo "      first 900 chars: $(echo "$chars" | head -c 900)"
    exit 1
fi
exit 0
