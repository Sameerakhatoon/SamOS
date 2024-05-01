#!/usr/bin/env bash
# e2e/185-disk-irq-fallback-q35.sh - samos.disk_irq_masked
# leaves the AHCI's IOAPIC entry masked, so no completion interrupt
# arrives. After three completions found by polling the driver polls
# for good, and the boot and a shell write still work. q35.
# e2e-requires: ahci apic (the AHCI's masked IOAPIC entry)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_diskirq_lib.sh"
E2E_MACHINE=q35
diskirq_check masked
echo "PASS: e2e/185 AHCI falls back to polling without its interrupt"
