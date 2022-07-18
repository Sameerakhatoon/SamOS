#!/bin/bash
# build.sh - thin wrapper that just runs `make`. The Makefile is the source
# of truth for build commands.

set -e
cd "$(dirname "$0")"
mkdir -p bin build
make all
echo "build: bin/boot.bin ($(stat -c%s bin/boot.bin) bytes)"
