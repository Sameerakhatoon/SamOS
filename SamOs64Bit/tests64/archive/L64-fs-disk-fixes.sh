#!/bin/bash
# tests64/L64-fs-disk-fixes.sh
#
# Lecture 64 - FS / disk bug fixes.
#
# diskstreamer_read goes from recursive to iterative.
# fat16_get_cluster_for_offset returns -EOUTOFRANGE on
# end-of-chain instead of -EIO, uses uint16_t, and treats
# 0xFFF8..0xFFFF as end of chain (not just 0xFF8 / 0xFFF
# which were a 12-bit-FAT artifact).
# fat16_read_internal_from_stream is iterative and returns
# bytes_read.
#
# For SIMPLE.BIN (2 bytes, one cluster) none of these matter.
# The test confirms the rewrites don't regress the existing
# user-enter path.

set -e
cd "$(dirname "$0")/.."

bash build.sh > /dev/null

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
