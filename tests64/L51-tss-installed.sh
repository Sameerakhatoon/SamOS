#!/bin/bash
# tests64/L51-tss-installed.sh
#
# Lectures 50 + 51 - TSS allocated, initialised, and installed
# into the GDT.
#
# kernel_main now:
#   - allocates a 1 MiB ring-0 stack via kzalloc
#   - unmaps the lowest page of the stack as a guard page
#   - memsets a struct tss to zero, fills rsp0 + iopb_offset
#   - writes the 16-byte TSS descriptor into the two reserved
#     GDT slots (7 + 8) via gdt_set_tss
#   - prints "tss ready"
#
# We don't ltr the TSS yet; the GDTR's limit was set at boot
# before slots 7 + 8 existed, so the CPU's view of the GDT
# stops at slot 6. A future commit will reload the GDT with the
# wider limit and then ltr - until then the TSS is "installed
# but invisible" - good enough to verify the descriptor write
# math without faulting.

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
for tok in 'Hello 64-bit!' 'multiheap ready' 'tss ready'; do
    echo "$chars" | grep -q "$tok" || { echo "FAIL: missing '$tok'"; ok=0; }
done

[ $ok -eq 1 ] || { echo "      first 240 chars: $(echo "$chars" | head -c 240)"; exit 1; }
exit 0
