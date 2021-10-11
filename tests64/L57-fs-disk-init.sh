#!/bin/bash
# tests64/L57-fs-disk-init.sh
#
# Lecture 57 - subsystem initializers.
#
# fs_init() registers the FAT16 driver in the generic VFS layer.
# disk_search_and_init() walks the ATA channels for primary
# disks and binds the filesystem driver to each.
#
# Either call panicking, faulting on a NULL deref, or kfree-ing
# something already kfree'd would silence the existing "tss
# load was fine" / "register isr80h" / "tss ready" tokens.
# Their presence after L57 is the smoke proof that both
# initializers returned cleanly.

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
for tok in 'multiheap ready' 'tss load was fine' 'register isr80h' 'tss ready'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
