#!/bin/bash
# The boot disk image of a non-x86 port, the same layout as the
# x86 bin/os.img that build.sh makes (sgdisk, mkfs.fat --offset
# and mtools on the plain file, no root):
#
#   p1 (1 MiB, FAT16, label ABC):  KERNEL.IMG and the programs; the e2e
#       harness puts CMDLINE.TXT here, as on the x86 ESP
#   p2 (FAT16, label SAMOS):       the kernel's "@:/": BLANK.ELF,
#       SHELL.ELF, HELLO.ELF, KTEST.ELF, SYSFONT.BMP, HELLO.TXT
#
#   tools/arch-osimg.sh <out.img> <kernel.img> <dir with blank.elf shell.elf hello.elf ktest.elf>
#
# Run from SamOs64Bit/ (the Makefile does). 700 MiB sparse, as on x86.
set -euo pipefail
out=$1 kernel=$2 elfs=$3
export MTOOLS_SKIP_CHECK=1 MTOOLS_NO_VFAT=1

IMG_MIB=700
SECTORS=$((IMG_MIB * 2048))
P1_START=2048
P2_START=$((SECTORS / 2))
P1_SECTORS=$((P2_START - P1_START))
P2_SECTORS=$((SECTORS - 2048 - P2_START))

tmp="$out.tmp.$$"
trap 'rm -f "$tmp"' EXIT
rm -f "$tmp"
truncate -s "${IMG_MIB}M" "$tmp"
sgdisk --clear \
       --new=1:$P1_START:$((P2_START - 1)) --typecode=1:EF00 --change-name=1:ESP \
       --new=2:$P2_START:$((P2_START + P2_SECTORS - 1)) --typecode=2:0700 --change-name=2:SAMOS \
       "$tmp" > /dev/null
part() {
    mkfs.fat -F 16 -n "$1" --offset "$2" -h "$2" "$tmp" $(($3 / 2)) 2>&1 > /dev/null \
        | { grep -v 'block count mismatch' || true; } >&2
}
part ABC   $P1_START $P1_SECTORS
part SAMOS $P2_START $P2_SECTORS
p1="$tmp@@$((P1_START * 512))"
p2="$tmp@@$((P2_START * 512))"

mcopy -i "$p1" "$kernel" ::/KERNEL.IMG
for p in "$p1" "$p2"; do
    for e in blank shell hello ktest; do
        mcopy -i "$p" "$elfs/$e.elf" "::/$(echo "$e" | tr a-z A-Z).ELF"
    done
    mcopy -i "$p" ./assets/sysfont.bmp ::/SYSFONT.BMP
done
mcopy -i "$p2" ./hello.txt ::/HELLO.TXT
mv -f "$tmp" "$out"
trap - EXIT
