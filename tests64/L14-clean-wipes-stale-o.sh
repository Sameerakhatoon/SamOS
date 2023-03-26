#!/bin/bash
# tests64/L14-clean-wipes-stale-o.sh
#
# Lecture 14 - Makefile cleanup safety net.
#
# Before L14, `make clean` only deleted files listed in FILES. If a
# module dropped out of FILES (or was renamed), its .o lingered under
# build/. Subsequent links could see stale code via -relocatable.
#
# L14 adds a `find build -type f -name "*.o" -delete` to clean. This
# test plants a forged stale object and asserts `make clean` removes
# it.

set -e
cd "$(dirname "$0")/.."

# Make sure build dir exists.
mkdir -p build/dummy_module

forged=build/dummy_module/forged.o
printf 'STALE' > "$forged"
[ -f "$forged" ] || { echo "FAIL: could not plant forged.o"; exit 1; }

make clean > /dev/null 2>&1 || true

if [ -f "$forged" ]; then
    echo "FAIL: stale .o survived make clean ($forged)"
    exit 1
fi
exit 0
