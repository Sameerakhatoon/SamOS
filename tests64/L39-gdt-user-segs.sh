#!/bin/bash
# tests64/L39-gdt-user-segs.sh
#
# Lecture 39 - user-mode GDT entries.
#
# Two new descriptors (selectors 0x28 and 0x30) join the GDT:
#   - 64-bit user code seg: DPL=3, L=1
#   - user data seg: DPL=3
#
# Nothing in the kernel uses them yet (no ring-3 entry path
# exists; that comes with the task subsystem in L40+). The GDT
# load happens at boot via lgdt in kernel.asm; if the new
# entries were malformed, lgdt itself would still succeed but
# the subsequent CS / DS selector loads would #GP.
#
# Smoke tokens unchanged from L38.

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
