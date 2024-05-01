#!/usr/bin/env bash
# e2e/176-apic-pc.sh - the same on pc (PIIX, IDE disk).
# e2e-requires: apic ide ps2 (the IOAPIC, LAPIC timer and masked 8259s of pc)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_apic_lib.sh"
E2E_MACHINE=pc
apic_check
echo "PASS: e2e/176 apic pc"
