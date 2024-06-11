#!/bin/bash
# The Raspberry Pi boot files and an SD card image with them:
#
#   <dir>/kernel8.img   bin/aarch64/kernel.img as it is: an arm64 Image
#                       (header with "ARM\x64"), which the Pi firmware
#                       loads at kernel_address and enters with x0 = its
#                       device tree, as QEMU's -kernel does
#   <dir>/config.txt    tools/pi/config.txt
#   <dir>/sdcard.img    256 MiB (a power of two, which QEMU's SD card
#                       needs), MBR, partition 1 from 1 MiB to the end:
#                       FAT32 (type 0x0c), label SAMOS, with kernel8.img,
#                       config.txt, the programs, SYSFONT.BMP, HELLO.TXT
#                       and ramdisk.img (the Pi 5's initramfs line). The
#                       label makes it the kernel's "@:/".
#
# The Pi firmware files (start4.elf, the board's .dtb, overlays) are not
# in the tree; copy them to the card from the raspberrypi/firmware repository.
#
#   tools/pi-sdimg.sh <dir> <kernel.img> <dir with blank.elf shell.elf hello.elf ktest.elf>
#
# Run from SamOs64Bit/ (make ARCH=aarch64 pi does).
set -euo pipefail
dir=$1 kernel=$2 elfs=$3
export MTOOLS_SKIP_CHECK=1 MTOOLS_NO_VFAT=1

IMG_MIB=256
START=2048
SECTORS=$((IMG_MIB * 2048 - START))

mkdir -p "$dir"
cp "$kernel" "$dir/kernel8.img"
cp ./tools/pi/config.txt "$dir/config.txt"

tmp="$dir/sdcard.img.tmp.$$"
trap 'rm -f "$tmp"' EXIT
rm -f "$tmp"
truncate -s "${IMG_MIB}M" "$tmp"
echo "start=$START, type=c" | sfdisk --quiet --label dos "$tmp"
mkfs.fat -F 32 -s 1 -n SAMOS --offset $START -h $START "$tmp" $((SECTORS / 2)) 2>&1 > /dev/null \
    | { grep -v 'block count mismatch' || true; } >&2
p="$tmp@@$((START * 512))"
mcopy -i "$p" "$dir/kernel8.img" ::/KERNEL8.IMG
mcopy -i "$p" "$dir/config.txt" ::/CONFIG.TXT
for e in blank shell hello ktest; do
    mcopy -i "$p" "$elfs/$e.elf" "::/$(echo "$e" | tr a-z A-Z).ELF"
done
mcopy -i "$p" ./assets/sysfont.bmp ::/SYSFONT.BMP
mcopy -i "$p" ./hello.txt ::/HELLO.TXT
mcopy -i "$p" ./bin/aarch64/ramdisk.img ::/RAMDISK.IMG    # the Pi 5's initramfs (config.txt)
mv -f "$tmp" "$dir/sdcard.img"
trap - EXIT
