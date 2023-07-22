#!/usr/bin/env bash
# tests64/run-all.sh - the single entry point for SamOS tests.
#
# Runs the e2e suite (tests64/e2e/[0-9]*-*.sh) and prints one
# pass/fail count. Arguments are passed through to e2e/run-all.sh:
#
#   bash tests64/run-all.sh            # everything, parallel
#   bash tests64/run-all.sh -j 1       # one at a time
#   bash tests64/run-all.sh 01- 31-    # a subset by name
#
# Build first with ./build.sh at the repo root.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
img="$here/../../bin/os.img"
if [ ! -f "$img" ]; then
    echo "tests64/run-all.sh: $img is missing; run ./build.sh first" >&2
    exit 2
fi
exec bash "$here/e2e/run-all.sh" "$@"
