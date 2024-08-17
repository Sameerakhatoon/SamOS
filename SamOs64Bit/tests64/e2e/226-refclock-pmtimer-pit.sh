#!/usr/bin/env bash
# e2e/226-refclock-pmtimer-pit.sh - the rest of the reference
# clock chain (refclock.c).
#   q35,hpet=off,pit=off  no HPET table and no PIT: the ACPI PM timer
#                         (ICH9, FADT PM_TMR_BLK, 24-bit)
#   pc,hpet=off           PIIX4's PM timer, though a PIT is there
#   q35 samos.refclock=pit  HPET and PM timer skipped: PIT channel 2,
#                         the last resort, as before
# Each: the LAPIC timer at QEMU's 62500 kHz, a 500 ms sleep never
# early, the shell answers.
# e2e-requires: apic ps2 (the LAPIC timer tick with each reference clock)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_refclock_lib.sh"

pm_re='^refclock: acpi pm timer at port 0x[0-9a-f]+, 3579 kHz, (24|32)-bit$'
refclock_check q35,hpet=off,pit=off "" "$pm_re" "ACPI PM timer" pmtimer
expect_no_serial '^refclock: hpet' "q35: no HPET table, no HPET"
refclock_check pc,hpet=off "" "$pm_re" "ACPI PM timer" pmtimer
refclock_check q35 "samos.refclock=pit" '^refclock: pit channel 2$' "PIT channel 2" pit
echo "PASS: e2e/226 calibration against the ACPI PM timer and PIT channel 2"
