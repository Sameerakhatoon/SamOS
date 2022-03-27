#!/bin/bash
# tests64/L71-restructure-uefi-stub.sh
#
# Lecture 71 - project restructured for UEFI boot path.
#
# All kernel sources / programs / build artefacts moved into
# SamOs64Bit/. Top-level holds the UEFI EDK2 application source
# (SamOs.c, SamOs.inf, SamOs*.uni, build.sh, run.sh).
#
# The .efi build requires an EDK2 environment we don't set up
# in CI. This test just confirms:
#   1. SamOs64Bit/build.sh still produces a working BIOS image.
#   2. The top-level UEFI source files are present.

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

# Top-level UEFI files present (from project root).
for f in ../SamOs.c ../SamOs.inf ../SamOs.uni ../SamOsStr.uni ../SamOsExtra.uni ../build.sh ../run.sh; do
    [ -f "$f" ] || { echo "FAIL: $f missing"; exit 1; }
done

# BIOS image still boots to user enter.
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
