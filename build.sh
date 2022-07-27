#!/bin/bash
# build.sh - exports the cross-compiler PATH and runs `make all`.

set -e
cd "$(dirname "$0")"

export PREFIX="$HOME/opt/cross"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"

mkdir -p bin build
make all
echo "build: bin/os.bin ($(stat -c%s bin/os.bin) bytes)"
