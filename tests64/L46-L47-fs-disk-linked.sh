#!/bin/bash
# tests64/L46-L47-fs-disk-linked.sh
#
# Lectures 46 + 47 - FAT16 / file / pparser / disk / streamer
# brought into the 64-bit build.
#
# Upstream's L46 (FAT16) leaves dangling diskstreamer_*
# references; L47 resolves them by adding disk.o + streamer.o.
# SamOs lands both in one commit so we never have an
# intermediate broken-link state.
#
# Source files unchanged from the 32-bit branch - they happen
# to be pointer-width-clean (no unsigned int casts of void*
# etc) so they recompile under x86_64-elf-gcc without source
# edits. ISERR / ERROR macros were widened to int64_t in this
# commit too, which makes the existing fs/disk code's error-
# path round trips safe.
#
# Nothing in kernel_main actually opens a file or reads a
# sector yet. The test confirms the link works.

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

dump=$(mktemp)
trap 'rm -f "$dump"' EXIT

( sleep 2; printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump" ) | timeout 15 \
    qemu-system-x86_64 -hda bin/os.bin -m 256 -accel tcg -display none -vga std \
        -monitor stdio -no-reboot > /dev/null 2>&1

chars=$(od -An -v -tx1 -w1 "$dump" \
            | awk 'NR%2==1 {print $1}' \
            | xxd -r -p \
            | tr '\0' ' ')

ok=1
for tok in 'Hello 64-bit!' 'multiheap ready' 'hello' 'Divide by zero error'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
