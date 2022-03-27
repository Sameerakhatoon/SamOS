#!/bin/bash
# tests64/L73-uefi-loader-stub.sh
#
# Lecture 73 - UEFI bootloader loads kernel.bin from FAT.
#
# SamOs.c grows from a print-and-spin stub into a real EDK2
# UEFI application: opens LoadedImageProtocol + SimpleFileSystem,
# reads kernel.bin from the partition we were booted from,
# AllocatePages at SAMOS_KERNEL_LOCATION (0x100000), CopyMem,
# ExitBootServices, jump.
#
# config.h gains SAMOS_KERNEL_LOCATION 0x100000 (used by both
# the .efi AND the BIOS path's kernel.asm jmp target).
#
# .efi build still requires EDK2; CI verifies the source file
# is syntactically reasonable + BIOS path still works.

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

# Source file checks (no EDK2, so we can't actually compile).
SAMOS_C=../SamOs.c
[ -f "$SAMOS_C" ] || { echo "FAIL: $SAMOS_C missing"; exit 1; }
grep -q 'ReadFileFromCurrentFilesystem' "$SAMOS_C" \
    || { echo "FAIL: ReadFileFromCurrentFilesystem missing"; exit 1; }
grep -q 'kernel.bin' "$SAMOS_C" \
    || { echo "FAIL: kernel.bin reference missing"; exit 1; }
grep -q 'ExitBootServices' "$SAMOS_C" \
    || { echo "FAIL: ExitBootServices missing"; exit 1; }
grep -q 'jmp \*%0' "$SAMOS_C" \
    || { echo "FAIL: indirect jump to kernel missing"; exit 1; }

grep -q '^#define SAMOS_KERNEL_LOCATION' src/config.h \
    || { echo "FAIL: SAMOS_KERNEL_LOCATION not in config.h"; exit 1; }

# BIOS path still boots to user enter.
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
