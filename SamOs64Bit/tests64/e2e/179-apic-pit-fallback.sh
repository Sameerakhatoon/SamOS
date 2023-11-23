#!/usr/bin/env bash
# e2e/179-apic-pit-fallback.sh - the APIC is up but the
# LAPIC timer is unusable (samos.lapic_timer_fail stands in for a
# calibration of 0). The 8259s stay masked and the PIT tick comes
# through IRQ0's IOAPIC entry: a sleeping task still wakes on time and
# the shell still answers. q35.
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"

cp "$E2E_IMAGE" "$E2E_TMP/os.img"
E2E_IMAGE="$E2E_TMP/os.img"
printf 'samos.lapic_timer_fail\n' > "$E2E_TMP/CMDLINE.TXT"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$E2E_IMAGE@@$((2048 * 512))" "$E2E_TMP/CMDLINE.TXT" ::/CMDLINE.TXT \
    || e2e_fail "mcopy CMDLINE.TXT"

E2E_MACHINE=q35
_mon() {
    shell_boot || return 0
    printf 'info pic\n'
    shell_cmd 'ktest.elf sleep 500' 60
    shell_cmd 'echo pit-fallback-ok'
    printf 'quit\n'
}
e2e_run_qemu 300 _mon

expect_serial '^apic: lapic id 0 version 0x[0-9a-f]+, ioapic gsi 0-23, 8259 masked 0x000000000000ffff$' "IOAPIC found, both 8259s masked"
expect_serial '^apic: IOAPIC with PIT tick$' "PIT tick through the IOAPIC reported"
expect_no_serial 'legacy 8259' "not reported as the legacy path"
expect_no_serial 'lapic timer [0-9]+ kHz' "LAPIC timer not calibrated or started"
expect_serial '^SLEEP 500 ms took' "a sleeping task woke on the PIT tick"
expect_serial '^pit-fallback-ok' "shell command typed through IRQ1"
expect_no_serial 'KERNEL PANIC|KERNEL FAULT' "no fault"
grep -Eq 'pic0: .*imr=ff .*irq_base=30' "$E2E_QEMU_LOG" || e2e_fail "master 8259 not masked at 0x30"
grep -Eq 'pic1: .*imr=ff .*irq_base=38' "$E2E_QEMU_LOG" || e2e_fail "slave 8259 not masked at 0x38"
echo "PASS: e2e/179 PIT tick through the IOAPIC"
