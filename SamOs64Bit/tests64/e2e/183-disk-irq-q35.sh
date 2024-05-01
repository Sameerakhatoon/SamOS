#!/usr/bin/env bash
# e2e/183-disk-irq-q35.sh - on q35 the AHCI completions
# come as interrupts: PCI INTx of 00:1f.2 on GSI 16, level, through
# the IOAPIC on vector 80. The boot, the BLANK.ELF selftest and a
# shell write and read all complete by interrupt. See _diskirq_lib.sh.
# e2e-requires: ahci apic (AHCI INTx through the IOAPIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_diskirq_lib.sh"
E2E_MACHINE=q35
diskirq_check irq
echo "PASS: e2e/183 AHCI completions through the IOAPIC"
