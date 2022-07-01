#!/bin/bash
# build.sh - top-level build for SamOs.
#
# Assembles boot.asm (sector 1) and data.asm (sector 2), then concatenates
# them into a 1024-byte disk image bin/os.bin.

set -e

cd "$(dirname "$0")"
mkdir -p bin build

nasm -f bin src/boot.asm -o bin/boot.bin
nasm -f bin src/data.asm -o bin/data.bin

# Build the disk image: sector 1 = boot.bin, sector 2 = data.bin.
cat bin/boot.bin bin/data.bin > bin/os.bin

echo "build: bin/os.bin ($(stat -c%s bin/os.bin) bytes)"
