#!/bin/bash
# build.sh - exports the cross-compiler PATH and runs `make all`.

set -e
cd "$(dirname "$0")"

export PREFIX="$HOME/opt/cross"
export TARGET=x86_64-elf
export PATH="$PREFIX/bin:$PATH"

mkdir -p bin build

# Force a full recompile every time. Our Makefile has no header
# dependencies, so any header edit (struct layout, prototype change)
# would otherwise leak stale .o files into the link.
make clean > /dev/null 2>&1 || true
make all EXTRA_CFLAGS="$EXTRA_CFLAGS"
echo "build: bin/os.bin ($(stat -c%s bin/os.bin) bytes)"
