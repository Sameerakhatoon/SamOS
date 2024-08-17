#!/usr/bin/env bash
# Shared body of e2e/175 (q35) and 176 (pc): interrupts through
# the IOAPIC and the LAPIC timer. Sourced after _lib.sh and
# _shell_lib.sh with E2E_MACHINE set. Not a test itself.

apic_check() {
    local m=$E2E_MACHINE
    _apic_mon() {
        shell_boot || return 0
        printf 'info pic\n'
        printf 'info lapic\n'
        sleep 2
        printf 'info lapic\n'
        shell_cmd 'echo apic-keys-ok'
        printf 'quit\n'
    }
    e2e_run_qemu 300 _apic_mon
    expect_serial '^apic: lapic id 0 version 0x[0-9a-f]+, ioapic gsi 0-23, 8259 masked 0x000000000000ffff$' "$m: IOAPIC found, both 8259s masked"
    expect_serial '^apic: lapic timer [0-9]+ kHz \(divide 16\), measured against HPET$' "$m: LAPIC timer calibrated (against the HPET, QEMU's default)"
    local khz
    khz=$(tr -d '\r' < "$E2E_SERIAL" | sed -nE 's/^apic: lapic timer ([0-9]+) kHz.*/\1/p' | head -n 1)
    # QEMU's APIC bus runs at 1 GHz: 62500 kHz after divide 16.
    [ "${khz:-0}" -ge 50000 ] && [ "$khz" -le 75000 ] || e2e_fail "$m: LAPIC timer $khz kHz, expected about 62500"
    e2e_log "$m: lapic timer $khz kHz"
    expect_no_serial 'legacy 8259' "$m: APIC mode, not the legacy path"
    expect_serial '^e2e: user selftest done' "$m: disk reads and BLANK.ELF selftest with the IOAPIC"
    expect_serial "$SHELL_BANNER" "$m: key press (IRQ1 through the IOAPIC) reached BLANK.ELF and the shell"
    expect_serial '^apic-keys-ok' "$m: shell command typed through IRQ1"
    expect_no_serial 'KERNEL PANIC|KERNEL FAULT' "$m: no fault"

    # QEMU's own view: both 8259s fully masked at 0x30/0x38, the
    # LAPIC timer periodic on vector 64, and counting.
    grep -Eq 'pic0: .*imr=ff .*irq_base=30' "$E2E_QEMU_LOG" || e2e_fail "$m: master 8259 not masked at 0x30"
    grep -Eq 'pic1: .*imr=ff .*irq_base=38' "$E2E_QEMU_LOG" || e2e_fail "$m: slave 8259 not masked at 0x38"
    grep -Eq 'LVTT.*periodic.*\(vec 64\)' "$E2E_QEMU_LOG" || e2e_fail "$m: LAPIC timer not periodic on vector 64"
    local counts
    counts=$(grep -oE 'current_count = [0-9]+' "$E2E_QEMU_LOG" | awk '{print $3}' | sort -u | wc -l)
    [ "$counts" -ge 2 ] || e2e_fail "$m: LAPIC timer current count did not change"
    e2e_log "$m: QEMU reports 8259s masked, LAPIC timer periodic and running"
}
