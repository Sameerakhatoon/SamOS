#!/usr/bin/env bash
# e2e/227-fblog-no-serial.sh - framebuffer logging. On a PC
# without a UART the boot's serial-only lines (acpi:, refclock:, tsc:,
# apic:, graphics:, ...) also reach the screen: kernel.c replays what
# was written before the system terminal existed and mirrors the rest
# until the first process starts (fblog_start / fblog_stop).
#   1. samos.fblog with COM1 present: the screen, read back with
#      screen_text.py from a screendump taken with the guest stopped,
#      shows serial-only lines that match the serial log.
#   2. No serial device at all (-serial none, nothing at 0x3F8): the
#      kernel finds no UART and puts the lines on the screen anyway;
#      polled by screendump, since there is no serial log to wait on.
# e2e-requires: x86 vga (COM1 removed with -serial none; QEMU's std VGA under OVMF)
. "$(dirname "$0")/_lib.sh"

font="$E2E_ROOT/assets/sysfont.bmp"
here="$(cd "$(dirname "$0")" && pwd)"
ppm="$E2E_TMP/screen.ppm"
txt="$E2E_TMP/screen.txt"
E2E_MACHINE=q35

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
_set_cmdline() {
    printf '%s\n' "$1" > "$E2E_TMP/CMDLINE.TXT"
    MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
        || e2e_fail "mcopy CMDLINE.TXT"
}

# The serial-only prefixes: kernel lines written with serial_write,
# never with print().
serial_only='^(acpi|apic|refclock|graphics|pci|ahci|disk|smp): '

# ---- 1. samos.fblog, COM1 present ----
_set_cmdline 'samos.fblog'
_mon_flag() {
    e2e_wait_serial '^fblog: serial lines on the screen \(samos\.fblog\)$' "${E2E_SHELL_BOOT_TIMEOUT:-240}" || true
    printf 'stop\n'
    sleep 1
    printf 'screendump "%s"\n' "$ppm"
    sleep 2
    printf 'quit\n'
}
e2e_run_qemu $((${E2E_SHELL_BOOT_TIMEOUT:-240} + 60)) _mon_flag
expect_serial '^fblog: serial lines on the screen \(samos\.fblog\)$' "samos.fblog: framebuffer logging on"
[ -s "$ppm" ] || e2e_fail "samos.fblog: no screendump"
python3 "$here/screen_text.py" "$ppm" "$font" > "$txt" || e2e_fail "screen_text.py failed"
# BLANK.ELF can start before the dump (the guest runs on while the
# monitor is told to stop, more of it on an idle host), and its
# windows and the mouse cursor then cover cells of these rows:
# screen_text.py reads them as '?', and screen_match.py matches the
# cells that are left against the serial log.
tr -d '\r' < "$E2E_SERIAL" > "$E2E_TMP/serial.txt"
python3 "$here/screen_match.py" "$txt" "$E2E_TMP/serial.txt" '^fblog: serial lines on the s' > "$E2E_TMP/match-fblog.txt"
grep -q '^match: ' "$E2E_TMP/match-fblog.txt" \
    || e2e_fail "samos.fblog: the fblog line is not on the screen ($(tail -n 5 "$txt"))"
# Every serial-only row read back is a line of the serial log, the
# covered cells aside, and there are some.
python3 "$here/screen_match.py" "$txt" "$E2E_TMP/serial.txt" "$serial_only" > "$E2E_TMP/match.txt"
bad=$(grep -m1 '^nomatch: ' "$E2E_TMP/match.txt")
[ -z "$bad" ] || e2e_fail "screen row not in the serial log: '${bad#nomatch: }'"
n=$(grep -c '^match: ' "$E2E_TMP/match.txt")
[ "$n" -ge 5 ] || e2e_fail "samos.fblog: only $n serial-only lines on the screen"
e2e_log "samos.fblog: $n serial-only lines on the screen, each in the serial log"
grep -Eq '^apic: lapic timer [0-9]+ k' "$txt" || e2e_fail "samos.fblog: the apic: lapic timer line is not on the screen"

# ---- 2. no serial port ----
_set_cmdline ''
e2e_qemu_args
for i in "${!E2E_QEMU_ARGS[@]}"; do
    [ "${E2E_QEMU_ARGS[$i]}" = "file:$E2E_SERIAL" ] && E2E_QEMU_ARGS[$i]=none
done
_mon_none() {
    local t
    for t in $(seq 1 $((${E2E_SHELL_BOOT_TIMEOUT:-300} / 5))); do
        sleep 4
        printf 'screendump "%s"\n' "$ppm.$t"
        sleep 1
        if [ -s "$ppm.$t" ] && python3 "$here/screen_text.py" "$ppm.$t" "$font" 2>/dev/null \
                | grep -q '^fblog: no serial port, serial lines o'; then
            printf 'stop\n'
            sleep 1
            printf 'screendump "%s"\n' "$ppm"
            sleep 2
            break
        fi
    done
    printf 'quit\n'
}
rm -f "$ppm" "$E2E_SERIAL"
_mon_none | timeout $((${E2E_SHELL_BOOT_TIMEOUT:-300} + 100)) "$E2E_QEMU_BIN" "${E2E_QEMU_ARGS[@]}" > "$E2E_QEMU_LOG" 2>&1 || true
[ ! -e "$E2E_SERIAL" ] || e2e_fail "no-serial run still wrote a serial log"
[ -s "$ppm" ] || e2e_fail "no serial: the fblog line never reached the screen"
python3 "$here/screen_text.py" "$ppm" "$font" > "$txt" || e2e_fail "screen_text.py failed"
# BLANK.ELF's windows soon cover the middle of these rows (the boot
# goes on after the dump that found the line), so prefixes only.
grep -q '^fblog: no serial port, serial lines o' "$txt" || e2e_fail "no serial: no fblog line"
n=$(grep -Ec "$serial_only" "$txt")
[ "$n" -ge 5 ] || e2e_fail "no serial: only $n serial-only lines on the screen"
grep -q '^refclock: hpet at 0x00000000fed00000, 100000 kHz, 64-bit$' "$txt" \
    || e2e_fail "no serial: the refclock line is not on the screen"
grep -Eq '^apic: lapic timer [0-9]+ k' "$txt" \
    || e2e_fail "no serial: the LAPIC timer line is not on the screen"
e2e_log "no serial: $n serial-only lines on the screen"
echo "PASS: e2e/227 framebuffer logging (samos.fblog, no serial port)"
