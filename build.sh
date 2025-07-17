#!/bin/bash
# build.sh - one-command build for SamOS: bin/os.img, no sudo.
#
#   1. The EDK2 UEFI loader SamOs.efi (top-level SamOs.c/.inf/.uni).
#   2. The kernel + user programs (SamOs64Bit/build.sh).
#   3. A 700 MiB GPT disk image, bin/os.img:
#        p1: FAT16 ESP  (label ABC)   /EFI/BOOT/BOOTX64.EFI, KERNEL.BIN, programs, font
#        p2: FAT16 data (label SAMOS) programs, font, HELLO.TXT
#
# SamOS divergence from the course book: the book composes
# the image with losetup + parted + mkfs.vfat + mount, which all need
# root. We write the GPT with sgdisk and the filesystems with
# mkfs.fat --offset / mtools `-i image@@offset`, all on the plain
# image file. Same layout, no sudo, and safe to run in parallel
# from several git worktrees. 
#
# Requirements: nasm, sgdisk (gdisk), mkfs.fat (dosfstools),
# mtools, the x86_64-elf cross compiler in ~/opt/cross, and EDK2 at
# ~/edk2 with BaseTools built (bootstrap.sh installs all of these).
#
# Environment:
#   EDK2_ROOT       EDK2 checkout (default ~/edk2)
#   EDK2_TOOLCHAIN  EDK2 toolchain tag (default GCC)
#   SAMOS_EFI       use this prebuilt SamOs.efi instead of building one
#   EXTRA_CFLAGS    passed through to the kernel build

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

: "${EDK2_TOOLCHAIN:=GCC}"
: "${EDK2_ROOT:=$HOME/edk2}"
export PATH="${PREFIX:-$HOME/opt/cross}/bin:$PATH"
export MTOOLS_SKIP_CHECK=1
export MTOOLS_NO_VFAT=1     # 8.3 names only: the kernel's FAT16 can't read LFNs yet

_missing=()
for tool in nasm sgdisk mkfs.fat mcopy mmd x86_64-elf-gcc x86_64-elf-ld; do
    command -v "$tool" >/dev/null 2>&1 || _missing+=("$tool")
done
if [ ${#_missing[@]} -gt 0 ]; then
    echo "build.sh: missing required tools: ${_missing[*]}" >&2
    echo "build.sh: run ./bootstrap.sh to install them." >&2
    exit 2
fi

mkdir -p "$ROOT/bin"

# -------------------------------------------------------------------
# Step 1: the UEFI loader.
# -------------------------------------------------------------------
EFI_OUT="$ROOT/bin/SamOs.efi"
if [ -n "${SAMOS_EFI:-}" ]; then
    cp "$SAMOS_EFI" "$EFI_OUT"
elif [ -x "$EDK2_ROOT/BaseTools/Source/C/bin/GenFw" ]; then
    # The EDK2 build is efi-build.sh's, shared with BOOTAA64.EFI.
    EDK2_ROOT="$EDK2_ROOT" EDK2_TOOLCHAIN="$EDK2_TOOLCHAIN" bash "$ROOT/efi-build.sh" X64 "$EFI_OUT"
elif [ -f "$EFI_OUT" ]; then
    echo "build.sh: EDK2 not found at $EDK2_ROOT; reusing existing bin/SamOs.efi" >&2
else
    echo "build.sh: EDK2 not found at $EDK2_ROOT and no bin/SamOs.efi to reuse." >&2
    echo "build.sh: run ./bootstrap.sh, or set SAMOS_EFI=/path/to/SamOs.efi." >&2
    exit 2
fi

# -------------------------------------------------------------------
# Step 2: kernel + user programs.
# -------------------------------------------------------------------
bash "$ROOT/SamOs64Bit/build.sh"

K="$ROOT/SamOs64Bit"
for f in "$K/bin/kernel.bin" "$K/programs/blank/blank.elf" "$K/programs/shell/shell.elf" "$K/programs/ktest/ktest.elf" "$K/assets/sysfont.bmp"; do
    [ -f "$f" ] || { echo "build.sh: missing build output $f" >&2; exit 1; }
done

# -------------------------------------------------------------------
# Step 3: the GPT disk image.
# -------------------------------------------------------------------
IMG="$ROOT/bin/os.img"
TMP_IMG="$IMG.tmp.$$"
trap 'rm -f "$TMP_IMG"' EXIT

IMG_MIB=700
SECTORS=$((IMG_MIB * 2048))
P1_START=2048                    # 1 MiB
P2_START=$((SECTORS / 2))        # 50%
P1_END=$((P2_START - 1))
P2_END=$((SECTORS - 2048 - 1))   # leave 1 MiB at the end for the backup GPT
P1_SECTORS=$((P1_END - P1_START + 1))
P2_SECTORS=$((P2_END - P2_START + 1))

rm -f "$TMP_IMG"
truncate -s "${IMG_MIB}M" "$TMP_IMG"
sgdisk --clear \
       --new=1:${P1_START}:${P1_END} --typecode=1:EF00 --change-name=1:ESP \
       --new=2:${P2_START}:${P2_END} --typecode=2:0700 --change-name=2:SAMOS \
       "$TMP_IMG" >/dev/null

# mkfs.fat's BLOCKS argument is in KiB. -h records the partition
# start as "hidden sectors", as mkfs.vfat does on a real partition.
# It warns that BLOCKS is smaller than the rest of the file; that is
# the point, so that one warning is filtered out.
mkfs_part() {
    mkfs.fat -F 16 -n "$1" --offset "$2" -h "$2" "$TMP_IMG" $(($3 / 2)) 2>&1 >/dev/null \
        | { grep -v 'block count mismatch' || true; } >&2
}
mkfs_part ABC   $P1_START $P1_SECTORS
mkfs_part SAMOS $P2_START $P2_SECTORS

P1="$TMP_IMG@@$((P1_START * 512))"
P2="$TMP_IMG@@$((P2_START * 512))"

# p1 (ESP): the UEFI loader reads KERNEL.BIN from the filesystem it
# was loaded from, so the kernel and user programs go here.
mmd   -i "$P1" ::/EFI ::/EFI/BOOT
mcopy -i "$P1" "$EFI_OUT"                      ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$P1" "$K/bin/kernel.bin"             ::/KERNEL.BIN
mcopy -i "$P1" "$K/programs/blank/blank.elf"   ::/BLANK.ELF
mcopy -i "$P1" "$K/programs/shell/shell.elf"   ::/SHELL.ELF
mcopy -i "$P1" "$K/programs/hello/hello.elf"   ::/HELLO.ELF
mcopy -i "$P1" "$K/assets/sysfont.bmp"         ::/SYSFONT.BMP

# p2 (SAMOS): the kernel's primary filesystem ("@:/").
mcopy -i "$P2" "$K/programs/blank/blank.elf"   ::/BLANK.ELF
mcopy -i "$P2" "$K/programs/shell/shell.elf"   ::/SHELL.ELF
mcopy -i "$P2" "$K/programs/hello/hello.elf"   ::/HELLO.ELF
mcopy -i "$P2" "$K/programs/ktest/ktest.elf"   ::/KTEST.ELF   # The interrupt work e2e probes
mcopy -i "$P2" "$K/programs/net/ping.elf"        ::/PING.ELF
mcopy -i "$P2" "$K/programs/net/udpecho.elf"     ::/UDPECHO.ELF
mcopy -i "$P2" "$K/programs/net/httpd.elf"       ::/HTTPD.ELF
mcopy -i "$P2" "$K/programs/net/wget.elf"        ::/WGET.ELF
mcopy -i "$P2" "$K/programs/net/polltest.elf"    ::/POLLTEST.ELF
mcopy -i "$P2" "$K/assets/sysfont.bmp"         ::/SYSFONT.BMP
mcopy -i "$P2" "$K/hello.txt"                  ::/HELLO.TXT

mv -f "$TMP_IMG" "$IMG"
trap - EXIT
echo "build.sh: bin/os.img ready ($(du -h --apparent-size "$IMG" | cut -f1) sparse, $(du -h "$IMG" | cut -f1) on disk)"
