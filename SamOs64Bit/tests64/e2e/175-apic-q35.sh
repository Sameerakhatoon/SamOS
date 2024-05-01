#!/usr/bin/env bash
# e2e/175-apic-q35.sh - on q35 the 8259s are masked, the
# keyboard IRQ comes through the IOAPIC, the scheduler tick is the
# LAPIC timer, and the AHCI boot still reaches the shell.
# e2e-requires: apic ahci ps2 (the IOAPIC, LAPIC timer and masked 8259s of q35)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_apic_lib.sh"
E2E_MACHINE=q35
apic_check
echo "PASS: e2e/175 apic q35"
