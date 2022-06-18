#!/bin/bash
# build.sh - top-level build for SamOs.
#
# Walks current build steps in order. Each chapter that adds a build step
# extends this script.

set -e

cd "$(dirname "$0")"
mkdir -p bin build

# Bootloader: assemble flat 512-byte binary.
nasm -f bin src/boot.asm -o bin/boot.bin

# So far the whole "OS" IS the bootloader. Boot image == boot sector.
cp bin/boot.bin bin/os.bin

echo "build: bin/os.bin ($(stat -c%s bin/os.bin) bytes)"
