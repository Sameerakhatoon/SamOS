#!/usr/bin/env bash
# e2e/161-fb-format.sh - the kernel honours boot_info.fb_format.
# QEMU's GOP is BGRX; CMDLINE.TXT forces another format through the
# samos.fb_format=<n> hook, then a screendump shows what reached the
# screen.
#   1 (RGBX): red and blue are swapped, so the red system-terminal
#             text shows up blue in QEMU's BGRX framebuffer.
#   2 (bitmask, unsupported): a serial line says so, the kernel never
#             touches the screen (only the loader's green fill is left)
#             and the boot still finishes the BLANK.ELF selftest.
. "$(dirname "$0")/_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
E2E_PPM="$E2E_TMP/screen.ppm"

_set_cmdline() {
    printf '%s\n' "$1" > "$E2E_TMP/CMDLINE.TXT"
    MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
        || e2e_fail "mcopy CMDLINE.TXT"
}

_screendump_script() {
    e2e_wait_serial "$E2E_SENTINEL" 150
    sleep 2
    printf 'screendump "%s"\n' "$E2E_PPM"
    sleep 2
    printf 'quit\n'
}

# Prints "<pure red> <pure blue> <pure green> <total>" pixel counts.
_count_pixels() {
    python3 - "$E2E_PPM" <<'PY'
import sys
data = open(sys.argv[1], "rb").read()
parts = data.split(b"\n", 3)
px = parts[3]
counts = {b"\xff\x00\x00": 0, b"\x00\x00\xff": 0, b"\x00\xff\x00": 0}
total = len(px) // 3
for i in range(0, total * 3, 3):
    k = px[i:i + 3]
    if k in counts:
        counts[k] += 1
print(counts[b"\xff\x00\x00"], counts[b"\x00\x00\xff"], counts[b"\x00\xff\x00"], total)
PY
}

_boot_with_format() {
    rm -f "$E2E_PPM"
    _set_cmdline "samos.fb_format=$1"
    e2e_run_qemu 240 _screendump_script
    [ -s "$E2E_PPM" ] || e2e_fail "fb_format=$1: no screendump ($(tail -n 3 "$E2E_QEMU_LOG"))"
    expect_serial "$E2E_SENTINEL" "fb_format=$1: BLANK.ELF selftest finished"
    expect_no_serial 'KERNEL PANIC|KERNEL FAULT|EARLY FAULT' "fb_format=$1: no panic"
}

_boot_with_format 1
expect_serial '^graphics: framebuffer format 1 \(RGBX\)$' "kernel took the RGBX format"
read -r red blue green total < <(_count_pixels)
e2e_log "fb_format=1: red $red, blue $blue, green $green of $total"
[ "$blue" -ge 1000 ] && [ "$red" -lt "$blue" ] \
    || e2e_fail "RGBX: red terminal text should reach the screen as blue (red $red, blue $blue)"

_boot_with_format 2
expect_serial '^graphics: framebuffer format 2 not supported, no screen output$' "bitmask format refused"
read -r red blue green total < <(_count_pixels)
e2e_log "fb_format=2: red $red, blue $blue, green $green of $total"
case " $E2E_ARCH_PROVIDES " in
*" uefi "*)
    [ "$green" -eq "$total" ] \
        || e2e_fail "refused format: the kernel drew on the screen (green $green of $total)" ;;
*)
    # No loader fill: the screen keeps what it started as, black
    # on riscv64's ramfb, all green on aarch64's.
    { [ "$green" -eq "$total" ] || [ $((red + blue + green)) -eq 0 ]; } \
        || e2e_fail "refused format: the kernel drew on the screen (red $red, blue $blue, green $green)" ;;
esac

echo "PASS: e2e/161 framebuffer pixel format"
