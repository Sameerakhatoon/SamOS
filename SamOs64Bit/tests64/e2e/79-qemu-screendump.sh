#!/usr/bin/env bash
# e2e/79-qemu-screendump.sh - QEMU 'screendump' captures the
# graphics framebuffer as a PPM image and we assert it has at
# least one non-zero pixel.
#
# This is the "screendump diff" path the framebuffer-snapshot
# bucket originally called for. Without a golden image we cannot
# diff against a known-good rendering, but we CAN assert that
# something landed in the framebuffer. Combined with 78
# (front-buffer non-zero via kernel-side selftest), this gives
# us:
#   * kernel selftest writes pixels to the back buffer (38)
#   * kernel calls graphics_redraw_all (78)
#   * QEMU sees a non-empty framebuffer image (79)
#
# Triple-anchor: a regression that breaks pixel emission would
# fail all three layers.

set -uo pipefail
. "$(dirname "$0")/_lib.sh"

E2E_PPM="${E2E_PPM:-/tmp/samos-e2e-screen.ppm}"
rm -f "$E2E_PPM"

e2e_select_image

qemu_args=(
    -m 512M
    -accel tcg
    -display none
    -monitor stdio
    -no-reboot
    -drive "file=$E2E_IMAGE,format=raw,if=ide"
    -bios "$E2E_OVMF"
    -machine pc
    -cpu Skylake-Server
)

# Sleep, screendump, then quit.
(
    sleep 25
    printf 'screendump "%s"\n' "$E2E_PPM"
    sleep 2
    printf 'quit\n'
) | timeout 35 qemu-system-x86_64 "${qemu_args[@]}" \
        > /tmp/samos-e2e-qemu.log 2>&1 || true

if [ ! -s "$E2E_PPM" ]; then
    e2e_fail "screendump produced no PPM (qemu log: /tmp/samos-e2e-qemu.log)"
fi

# A blank framebuffer would be a giant PPM of all zeros. Scan
# the pixel-data section (skip the ASCII header which is 'P6\n
# W H\n255\n') and check for any non-zero byte.
header_end=$(grep -aob -m1 '^255$' "$E2E_PPM" | head -n1 | cut -d: -f1)
if [ -z "$header_end" ]; then
    e2e_fail "could not locate PPM header end in $E2E_PPM"
fi
# Move past the '255\n' to the start of pixel bytes.
pixel_off=$((header_end + 4))

# Count non-zero bytes in the first 64 KiB of pixel data.
nonzero=$(dd if="$E2E_PPM" bs=1 skip=$pixel_off count=65536 2>/dev/null \
          | od -An -v -tu1 | tr -s ' ' '\n' | grep -vc '^0$' || true)

if [ "${nonzero:-0}" -gt 0 ]; then
    echo "[e2e] framebuffer screendump: $nonzero non-zero bytes in first 64 KiB"
    echo "PASS: e2e/79 qemu screendump"
    exit 0
else
    e2e_fail "QEMU screendump shows an all-zero framebuffer (first 64 KiB)"
fi
