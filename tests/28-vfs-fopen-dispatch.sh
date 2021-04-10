#!/bin/bash
# tests/28-vfs-fopen-dispatch.sh
#
# Ch 70 - VFS fopen now parses the path, looks up the disk, validates
# the mode, and dispatches to the disk's filesystem->open. fat16_open
# is still a stub returning null, but null is not ISERR, so fopen
# proceeds to allocate a descriptor and returns its index (1 for the
# first call). Three probes verify the dispatch:
#   fop_ok=00000001  -> well-formed path + valid mode -> descriptor 1
#   fop_bad=00000000 -> path parser rejects "notapath" -> fopen returns 0
#   fop_mod=00000000 -> unknown mode 'z' -> fopen returns 0

set -e
cd "$(dirname "$0")/.."

./build.sh > /dev/null

dump=$(mktemp)
cmd=$(mktemp)
trap 'rm -f "$dump" "$cmd"' EXIT

printf 'pmemsave 0xb8000 4096 "%s"\nquit\n' "$dump" > "$cmd"

( sleep 7; cat "$cmd" ) | timeout 25 qemu-system-x86_64 \
        -hda bin/os.bin \
        -m 256 \
        -accel tcg \
        -display none \
        -vga std \
        -monitor stdio \
        -no-reboot \
        > /dev/null 2>&1

chars=$(od -An -v -tx1 -w1 "$dump" \
            | awk 'NR%2==1 {printf "%s\n", $1}' \
            | xxd -r -p \
            | tr '\0' ' ')

ok=1
echo "$chars" | grep -q 'fop_ok=00000001'   || { echo "FAIL: fop_ok != 1"; ok=0; }
echo "$chars" | grep -q 'fop_bad=00000000'  || { echo "FAIL: fop_bad != 0"; ok=0; }
echo "$chars" | grep -q 'fop_mod=00000000'  || { echo "FAIL: fop_mod != 0"; ok=0; }

if [ $ok -ne 1 ]; then
    echo "      first 900 chars: $(echo "$chars" | head -c 900)"
    exit 1
fi
exit 0
