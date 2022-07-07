#!/bin/bash
# build.sh - top-level build for SamOs.
#
# Assembles boot.asm (sector 1) into a flat binary that is also the disk
# image. data.asm was used as a second sector for one chapter and is no
# longer needed; the next stage will be a multi-sector kernel image we
# load from inside the boot sector.

set -e

cd "$(dirname "$0")"
mkdir -p bin build

nasm -f bin src/boot.asm -o bin/boot.bin
cp bin/boot.bin bin/os.bin

echo "build: bin/os.bin ($(stat -c%s bin/os.bin) bytes)"
