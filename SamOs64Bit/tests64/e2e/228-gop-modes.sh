#!/usr/bin/env bash
# e2e/228-gop-modes.sh - the loader's GOP mode choice on firmware
# with unusual modes (SamOs.c GopModeScore). OVMF's QemuVideoDxe
# starts in the display's EDID resolution (xres/yres):
#   1. VGA at 2560 x 1600: over the 1920 x 1200 limit, so the loader
#      switches to the usable mode closest to 1024 x 768.
#   2. bochs-display at 1000 x 700: odd size, BGRX, within the limit:
#      kept as is.
#   3. no display device (-vga none): no GOP at all. The loader boots
#      without screen output instead of stopping (before, x86
#      returned EFI_NOT_FOUND), the kernel refuses format 2 and the
#      boot still finishes the BLANK.ELF selftest.
# 1 and 2 read the screen back (screen_text.py, samos.fblog on): the
# rows are lines of the serial log, so the kernel drew with the pitch
# and size the loader passed.
# e2e-requires: x86 uefi vga (OVMF's GOP on QEMU's VGA and bochs-display)
. "$(dirname "$0")/_lib.sh"

font="$E2E_ROOT/assets/sysfont.bmp"
here="$(cd "$(dirname "$0")" && pwd)"
ppm="$E2E_TMP/screen.ppm"
E2E_MACHINE=q35

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.fblog\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

_mon() {
    e2e_wait_serial '^fblog: serial lines on the screen' "${E2E_SHELL_BOOT_TIMEOUT:-240}" || true
    printf 'stop\n'
    sleep 1
    printf 'screendump "%s"\n' "$ppm"
    sleep 2
    printf 'cont\n'
    e2e_wait_serial "$E2E_SENTINEL" "${E2E_SHELL_BOOT_TIMEOUT:-240}" || true
    printf 'quit\n'
}

# _boot <name> <qemu args>: boot and check the selftest finished.
_boot() {
    local name=$1
    shift
    rm -f "$ppm"
    E2E_EXTRA_QEMU_ARGS="$*"
    e2e_run_qemu $((2 * ${E2E_SHELL_BOOT_TIMEOUT:-240} + 60)) _mon
    cp "$E2E_SERIAL" "$E2E_TMP/serial-$name.log"
    expect_serial "$E2E_SENTINEL" "$name: BLANK.ELF selftest finished"
    expect_no_serial 'KERNEL PANIC|KERNEL FAULT|EARLY FAULT' "$name: no panic"
}

# _screen_ok <name> <w> <h>: the screendump has that size and at least
# 5 rows that are serial log lines (a window or the mouse cursor may
# cover part of the boot text: those cells are left out).
_screen_ok() {
    local name=$1 w=$2 h=$3
    [ -s "$ppm" ] || e2e_fail "$name: no screendump"
    head -c 20 "$ppm" | tr '\n' ' ' | grep -q "^P6 $w $h " \
        || e2e_fail "$name: screendump is not $w x $h ($(head -c 20 "$ppm" | tr '\n' ' '))"
    python3 "$here/screen_text.py" "$ppm" "$font" > "$E2E_TMP/screen-$name.txt" || e2e_fail "screen_text.py failed"
    tr -d '\r' < "$E2E_SERIAL" > "$E2E_TMP/serial.txt"
    # Cells under BLANK.ELF's windows read as '?' (screen_match.py).
    local n
    n=$(python3 "$here/screen_match.py" "$E2E_TMP/screen-$name.txt" "$E2E_TMP/serial.txt" '^[a-z]+: ' | grep -c '^match: ')
    [ "$n" -ge 5 ] || e2e_fail "$name: only $n screen rows match serial lines (pitch or size wrong?)"
    e2e_log "$name: ${w}x${h} screen, $n rows read back as serial lines"
}

_boot big -vga none -device VGA,xres=2560,yres=1600,vgamem_mb=64
expect_serial '^GOP: [0-9]+ modes, current [0-9]+ \(2560 x 1600, pitch 2560, format 1\)$' "big: firmware mode 2560 x 1600, not kept"
expect_serial '^graphics: framebuffer 1024x768 pitch 1024, ' "big: switched to 1024 x 768"
_screen_ok big 1024 768

_boot odd -vga none -device bochs-display,xres=1000,yres=700
expect_serial '^GOP: [0-9]+ modes, current [0-9]+ \(1000 x 700, pitch 1000, format 1\), kept$' "odd: 1000 x 700 kept"
expect_serial '^graphics: framebuffer 1000x700 pitch 1000, ' "odd: kernel at 1000 x 700"
_screen_ok odd 1000 700

_boot none -vga none
expect_serial '^No usable framebuffer: booting without screen output$' "none: the loader goes on without a GOP"
expect_serial '^graphics: framebuffer format 2 not supported, no screen output$' "none: the kernel draws nowhere"
echo "PASS: e2e/228 GOP mode choice (too large, odd size, no display)"
