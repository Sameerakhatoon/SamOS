#!/bin/bash
# tests/26-fat16-resolver.sh
#
# Ch 68 - fat16_resolve now reads the BPB off the disk, validates the
# EBPB signature (0x29 -> match), allocates a fat_private blob, and
# walks the root directory. Two checks:
#   - disk.fs_private is non-null after resolve (kernel prints "pnz=00000001")
#   - the root directory has zero entries because the volume is
#     zero-filled with no files (kernel prints "rdt=00000000")
# If the signature check or the directory walk explodes, the resolver
# rolls back and disk.fs_private goes back to null, so pnz would be 0.

set -e
cd "$(dirname "$0")/.."
source tests/_lib.sh

./build.sh > /dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

run_kernel_capture "$log"

chars=$(cat "$log")

ok=1
echo "$chars" | grep -q 'pnz=00000001'  || { echo "FAIL: pnz != 1 (resolver did not retain fs_private)"; ok=0; }
# Ch 137 added shell.elf alongside hello.txt + blank.bin + blank.elf;
# root dir is now 4.
echo "$chars" | grep -q 'rdt=00000004'  || { echo "FAIL: rdt != 4 (expected hello.txt + blank.bin + blank.elf + shell.elf)"; ok=0; }

if [ $ok -ne 1 ]; then
    echo "      first 900 chars: $(echo "$chars" | head -c 900)"
    exit 1
fi
exit 0
