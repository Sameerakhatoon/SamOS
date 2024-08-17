#!/usr/bin/env bash
# e2e/225-refclock-no-pit.sh - a PC without an 8254 PIT. QEMU
# q35 and pc with pit=off: the TSC and the LAPIC timer are calibrated
# against the HPET from the ACPI HPET table, the scheduler ticks on
# the LAPIC timer, a sleeping task wakes on time and the shell answers.
# Before the HPET and PM-timer calibration the PIT channel 2 calibration measured the LAPIC timer
# at 1 kHz and the TSC at 78 kHz here, and the boot never reached the
# shell.
# e2e-requires: apic ps2 (the LAPIC timer tick on q35 and pc with QEMU's PIT removed)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_refclock_lib.sh"

hpet_re='^refclock: hpet at 0x00000000fed00000, 100000 kHz, 64-bit$'
refclock_check q35,pit=off "" "$hpet_re" HPET hpet
refclock_check pc,pit=off "" "$hpet_re" HPET hpet
echo "PASS: e2e/225 calibration against the HPET without a PIT (q35, pc)"
