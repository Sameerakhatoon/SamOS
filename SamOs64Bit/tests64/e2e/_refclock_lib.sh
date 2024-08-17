#!/usr/bin/env bash
# Shared body of e2e/225 and 226: the TSC and LAPIC timer
# calibrated against the reference clock (refclock.c: HPET, ACPI PM
# timer, PIT channel 2), with QEMU's PIT or HPET switched off. Sourced
# after _lib.sh and _shell_lib.sh. Not a test itself.
#
# refclock_check <machine> <cmdline or ""> <refclock line ERE> <apic name> <tsc name>
#   Boots to the shell, sleeps 500 ms in KTEST.ELF, types a command.
#   Checks the refclock: line, the LAPIC timer's rate against QEMU's
#   fixed 1 GHz APIC bus (62500 kHz after divide 16: a wrong reference
#   clock rate shows here), the tsc: line, a sleep that never wakes
#   early, keys through IRQ1 and QEMU's view of a periodic LAPIC timer.

refclock_check() {
    local m=$1 cmdline=$2 ref_re=$3 apic_name=$4 tsc_name=$5
    cp "$E2E_OS_IMG" "$E2E_TMP/os.img"
    E2E_IMAGE="$E2E_TMP/os.img"
    if [ -n "$cmdline" ]; then
        printf '%s\n' "$cmdline" > "$E2E_TMP/CMDLINE.TXT"
        MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
            || e2e_fail "$m: mcopy CMDLINE.TXT"
    fi
    E2E_MACHINE=$m
    _refclock_mon() {
        shell_boot || return 0
        shell_cmd 'ktest.elf sleep 500' 60
        printf 'info lapic\n'
        sleep 1
        printf 'info lapic\n'
        shell_cmd 'echo refclock-ok'
        printf 'quit\n'
    }
    e2e_run_qemu $((2 * ${E2E_SHELL_BOOT_TIMEOUT:-120} + 160)) _refclock_mon
    cp "$E2E_SERIAL" "$E2E_TMP/serial-${m//[,=]/_}.log"

    expect_serial "$ref_re" "$m: reference clock line"
    expect_serial "^apic: lapic timer [0-9]+ kHz \\(divide 16\\), measured against $apic_name\$" "$m: LAPIC timer measured against $apic_name"
    local khz
    khz=$(tr -d '\r' < "$E2E_SERIAL" | sed -nE 's/^apic: lapic timer ([0-9]+) kHz.*/\1/p' | head -n 1)
    # QEMU's APIC bus runs at 1 GHz: 62500 kHz after divide 16.
    [ "${khz:-0}" -ge 56250 ] && [ "$khz" -le 68750 ] \
        || e2e_fail "$m: LAPIC timer $khz kHz, expected 62500 within 10 %"
    e2e_log "$m: lapic timer $khz kHz against $apic_name"
    expect_serial "^tsc: cpuid [0-9]+ kHz, $tsc_name [1-9][0-9]* kHz, using [1-9][0-9]* kHz\$" "$m: TSC measured against $tsc_name"
    expect_no_serial 'IOAPIC with PIT tick|legacy 8259|timer: no PIT' "$m: the LAPIC timer ticks, no PIT fallback"
    local took
    took=$(tr -d '\r' < "$E2E_SERIAL" | sed -n 's/^SLEEP 500 ms took \([0-9]*\)$/\1/p' | head -n 1)
    [ -n "$took" ] || e2e_fail "$m: no SLEEP line (no tick wakeup?)"
    # Never early; late only by host load (e2e/163).
    [ "$took" -ge 500 ] || e2e_fail "$m: sleep_ms(500) returned early, after $took ms"
    e2e_log "$m: sleep_ms(500) took $took ms"
    expect_serial '^refclock-ok' "$m: shell command typed through IRQ1"
    expect_no_serial 'KERNEL PANIC|KERNEL FAULT' "$m: no fault"
    grep -Eq 'LVTT.*periodic.*\(vec 64\)' "$E2E_QEMU_LOG" || e2e_fail "$m: LAPIC timer not periodic on vector 64"
    local counts
    counts=$(grep -oE 'current_count = [0-9]+' "$E2E_QEMU_LOG" | awk '{print $3}' | sort -u | wc -l)
    [ "$counts" -ge 2 ] || e2e_fail "$m: LAPIC timer current count did not change"
}
