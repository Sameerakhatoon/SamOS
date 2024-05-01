#!/usr/bin/env bash
# e2e/184-disk-irq-pc.sh - on pc the PATA completions come
# as interrupts: IRQ14 on GSI 14, edge, through the IOAPIC on vector
# 82. The boot, the BLANK.ELF selftest and a shell write and read all
# complete by interrupt. See _diskirq_lib.sh.
# e2e-requires: ide apic (PATA IRQ14 through the IOAPIC)
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_diskirq_lib.sh"
E2E_MACHINE=pc
diskirq_check irq
echo "PASS: e2e/184 PATA completions through the IOAPIC"
