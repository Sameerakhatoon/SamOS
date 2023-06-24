#!/bin/bash
# Lecture 71 - top-level build orchestrator for SamOs.
#
# Combines two builds:
#   1. The EDK2 UEFI application SamOs.efi (top-level *.c/*.inf/*.uni).
#      Requires EDK2 cloned at $HOME/edk2 with BaseTools built and the
#      module symlinked under MdeModulePkg/Application/SamOs/.
#   2. The inner kernel + user-program build (SamOs64Bit/build.sh).
#
# This script then composes a 700 MiB GPT-partitioned disk image:
#   p1: FAT16 ESP holding the UEFI bootloader at /EFI/BOOT/BOOTX64.EFI
#   p2: FAT16 SamOs partition for the kernel + user programs
#
# Run requirements (NOT auto-installed here):
#   - EDK2 / BaseTools (https://github.com/tianocore/edk2)
#   - parted, mkfs.vfat, losetup (need sudo for these)
#   - OVMF firmware (Debian/Ubuntu: apt install ovmf -> /usr/share/ovmf/OVMF.fd)
#
# SamOs deviation from upstream PeachOS64 L71 build.sh: variable names
# adjusted to SamOs prefix. Disk image renamed bin/PeachOS.efi ->
# bin/SamOs.efi.

set -e

CURRENT_DIR=$(pwd)

# -------------------------------------------------------------------
# Step 1: EDK2 UEFI application build.
# -------------------------------------------------------------------
# Upstream's build.sh cd's three levels up to reach the EDK2 root
# (../../../ from MdeModulePkg/Application/PeachOS/). We mirror that
# expectation: this script is run from the SamOs project root, which
# in an EDK2 setup is symlinked under MdeModulePkg/Application/SamOs/.
#
# If EDK2 isn't set up, skip this step and just build the BIOS path.
if [ -d ../../../BaseTools ]; then
    cd ../../../
    export EDK_TOOLS_PATH=$HOME/edk2/BaseTools
    . edksetup.sh BaseTools
    build
    cd "$CURRENT_DIR"
else
    echo "EDK2 not detected at ../../../BaseTools - skipping UEFI build."
    echo "(see file header for setup notes)"
fi

# -------------------------------------------------------------------
# Step 2: build the inner kernel + user programs.
# -------------------------------------------------------------------
cd "$CURRENT_DIR/SamOs64Bit"
bash build.sh
cd "$CURRENT_DIR"

# -------------------------------------------------------------------
# Step 3 (UEFI-only): compose the GPT-partitioned disk image.
# -------------------------------------------------------------------
if [ ! -f ../../../Build/MdeModule/DEBUG_GCC/X64/SamOs.efi ]; then
    echo "SamOs.efi not produced - skipping disk image composition."
    echo "BIOS path image is at SamOs64Bit/bin/os.bin (16 MiB)."
    exit 0
fi

mkdir -p ./bin /mnt/d

dd if=/dev/zero bs=1048576 count=700 of=./bin/os.img
LOOPDEV=$(sudo losetup --find --show --partscan ./bin/os.img)
echo "Loop Device $LOOPDEV"

# GPT partition table.
sudo parted "$LOOPDEV" --script mklabel gpt
# ESP for the bootloader.
sudo parted "$LOOPDEV" --script mkpart ESP fat16 1Mib 50%
# SamOs partition for the kernel.
sudo parted "$LOOPDEV" --script mkpart ESP fat16 50% 100%
sudo parted "$LOOPDEV" --script set 1 esp on
sudo partprobe "$LOOPDEV"
sleep 2
lsblk "$LOOPDEV"

sudo mkfs.vfat -n ABC   "${LOOPDEV}p1"
sudo mkfs.vfat -n SAMOS "${LOOPDEV}p2"

sudo mount -t vfat "${LOOPDEV}p2" /mnt/d
# (kernel copy into /mnt/d would go here once the UEFI path actually
# loads it from p2; in upstream L71 this is just a placeholder.)
sudo umount /mnt/d

cp ../../../Build/MdeModule/DEBUG_GCC/X64/SamOs.efi ./bin/SamOs.efi
sudo mkdir -p /mnt/d/EFI/BOOT
sudo cp ./bin/SamOs.efi /mnt/d/EFI/Boot/BOOTX64.efi
sudo umount /mnt/d || true
sudo losetup -d "$LOOPDEV"

echo "Build completed"
