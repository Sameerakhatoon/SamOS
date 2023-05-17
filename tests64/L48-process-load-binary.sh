#!/bin/bash
# tests64/L48-process-load-binary.sh
#
# Lecture 48 - process_load_binary un-stubbed. Now that FAT16
# and disk landed in L46-L47, the fopen / fstat / fread /
# fclose calls work and process_load_binary can read a real
# program file off the disk image.
#
# Nothing in kernel_main calls process_load_binary yet (need
# the task subsystem fully wired). Test just confirms the
# build links with the live function body.

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
