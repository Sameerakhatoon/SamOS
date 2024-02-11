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
# EDK2 toolchain auto-detection. Modern Ubuntu (24+) ships gcc 13
# which maps to EDK2's GCCNOLTO tag. Older boxes may use GCC5.
# Override via $EDK2_TOOLCHAIN if needed.
: "${EDK2_TOOLCHAIN:=GCCNOLTO}"
: "${EDK2_ROOT:=$HOME/edk2}"

# If EDK2 isn't set up, skip this step and just build the BIOS path.
if [ -d "$EDK2_ROOT/BaseTools" ]; then
    cd "$EDK2_ROOT"
    export EDK_TOOLS_PATH="$EDK2_ROOT/BaseTools"
    . edksetup.sh BaseTools
    build -p MdeModulePkg/MdeModulePkg.dsc \
          -m MdeModulePkg/Application/SamOs/SamOs.inf \
          -a X64 -t "$EDK2_TOOLCHAIN"
    cd "$CURRENT_DIR"
else
    echo "EDK2 not detected at $EDK2_ROOT/BaseTools - skipping UEFI build."
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
EDK2_OUT="$EDK2_ROOT/Build/MdeModule/DEBUG_${EDK2_TOOLCHAIN}/X64/SamOs.efi"
if [ ! -f "$EDK2_OUT" ]; then
    echo "SamOs.efi not produced at $EDK2_OUT - skipping disk image composition."
    echo "BIOS path image is at SamOs64Bit/bin/os.bin (16 MiB)."
    exit 0
fi

mkdir -p ./bin /tmp/samos-mnt

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

# Mount the ESP. The UEFI loader reads kernel.bin from the same
# filesystem it was loaded from (ReadFileFromCurrentFilesystem),
# so the kernel + user ELFs go HERE, not on partition 2.
sudo mount -t vfat "${LOOPDEV}p1" /tmp/samos-mnt
sudo mkdir -p /tmp/samos-mnt/EFI/BOOT
cp "$EDK2_OUT" ./bin/SamOs.efi
sudo cp ./bin/SamOs.efi /tmp/samos-mnt/EFI/BOOT/BOOTX64.EFI
sudo cp ./SamOs64Bit/bin/kernel.bin /tmp/samos-mnt/kernel.bin
sudo cp ./SamOs64Bit/programs/blank/blank.elf /tmp/samos-mnt/BLANK.ELF || true
sudo cp ./SamOs64Bit/programs/shell/shell.elf /tmp/samos-mnt/SHELL.ELF || true
# G48 unblock: stage the (stub) system font BMP so the kernel's
# terminal_create path runs at boot. A real upstream sysfont
# would replace this 9x16-cell white-block stub.
sudo cp ./SamOs64Bit/assets/sysfont.bmp /tmp/samos-mnt/sysfont.bmp || true
sudo umount /tmp/samos-mnt

# Also stage the ELF programs onto the SAMOS partition (p2) so
# kernel-side `fopen("@:/BLANK.ELF")` resolves regardless of which
# FAT partition the kernel's disk driver picks as primary.
sudo mount -t vfat "${LOOPDEV}p2" /tmp/samos-mnt
sudo cp ./SamOs64Bit/programs/blank/blank.elf /tmp/samos-mnt/BLANK.ELF || true
sudo cp ./SamOs64Bit/programs/shell/shell.elf /tmp/samos-mnt/SHELL.ELF || true
sudo cp ./SamOs64Bit/assets/sysfont.bmp /tmp/samos-mnt/sysfont.bmp || true
sudo umount /tmp/samos-mnt
sudo losetup -d "$LOOPDEV"

echo "Build completed (bin/os.img + bin/SamOs.efi)"
