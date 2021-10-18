#!/bin/bash
# tests64/L63-elfloader-linked.sh
#
# Lecture 63 - elf.c + elfloader.c added to the build,
# process_load_elf + process_map_elf un-stubbed.
#
# blank.elf + shell.elf land on the disk image alongside
# SIMPLE.BIN. kernel still loads SIMPLE.BIN (raw flat) - the
# ELF loader code is wired but actually using it on our ELF64
# files needs L65's refactor.

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

# Files must all be on the FAT16 disk.
for f in 'SIMPLE *BIN' 'BLANK *ELF' 'SHELL *ELF'; do
    mdir -i bin/os.bin :: 2>/dev/null | grep -q "$f" \
        || { echo "FAIL: $f missing from os.bin"; exit 1; }
done

# elf.o + elfloader.o must be in the build.
for o in elf.o elfloader.o; do
    [ -f "build/loader/formats/$o" ] \
        || { echo "FAIL: build/loader/formats/$o missing"; exit 1; }
done

dump=$(mktemp)
trap 'rm -f "$dump"' EXIT

( sleep 5; printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump" ) | timeout 12 \
    qemu-system-x86_64 -hda bin/os.bin -m 256 -accel tcg -display none -vga std \
        -monitor stdio -no-reboot > /dev/null 2>&1

chars=$(od -An -v -tx1 -w1 "$dump" \
            | awk 'NR%2==1 {print $1}' \
            | xxd -r -p \
            | tr '\0' ' ')

ok=1
for tok in 'tss ready' 'user enter'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
