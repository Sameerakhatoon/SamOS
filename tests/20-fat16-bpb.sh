#!/bin/bash
# tests/20-fat16-bpb.sh
#
# Ch 61 - boot.bin carries a FAT16 BIOS Parameter Block at offset 3.
# Ch 75 - the Makefile now also runs mformat to lay down real FAT
# structures (so we can mcopy hello.txt). mformat -k preserves the
# jmp short + OEM identifier; it rewrites the numerical BPB fields
# but also recomputes SectorsPerCluster, so we only assert on the
# fields that *must* survive end-to-end for the kernel to mount FAT16:
#   - OEM identifier at offset 3..10 = "SAMOS   "
#   - BytesPerSector at offset 11..12 = 0x0200 little-endian
#   - SystemIDString at offset 54..61 = "FAT16   "
#   - Boot signature still at 510..511 = 0x55 0xAA
# Also verify os.bin is at least 16 MiB + boot + kernel.

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

img=bin/os.bin

oem=$(dd if="$img" bs=1 skip=3   count=8 2>/dev/null)
bps=$(dd if="$img" bs=1 skip=11  count=2 2>/dev/null | od -An -tx1 | tr -d ' \n')
sid=$(dd if="$img" bs=1 skip=54  count=8 2>/dev/null)
sig=$(dd if="$img" bs=1 skip=510 count=2 2>/dev/null | od -An -tx1 | tr -d ' \n')

ok=1
[ "$oem" = "SAMOS   " ]   || { echo "FAIL: OEM != 'SAMOS   ' (got '$oem')"; ok=0; }
[ "$bps" = "0002" ]       || { echo "FAIL: BytesPerSector != 0x0200 LE (got $bps)"; ok=0; }
[ "$sid" = "FAT16   " ]   || { echo "FAIL: SystemID != 'FAT16   ' (got '$sid')"; ok=0; }
[ "$sig" = "55aa" ]       || { echo "FAIL: Boot signature != 55 AA (got $sig)"; ok=0; }

# 16 MiB padding + boot + kernel = at least 16777216 + 512 + 16K bytes.
size=$(stat -c%s "$img")
if [ "$size" -lt 16793600 ]; then
    echo "FAIL: os.bin smaller than expected ($size bytes)"
    ok=0
fi

[ $ok -eq 1 ] || exit 1
exit 0
