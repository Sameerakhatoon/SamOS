#!/usr/bin/env bash
# e2e/186-disk-irq-fallback-pc.sh - samos.disk_irq_masked
# on pc: IRQ14's IOAPIC entry stays masked, the PATA driver falls back
# to polling after three completions, the boot and the shell still
# work.
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_diskirq_lib.sh"
E2E_MACHINE=pc
diskirq_check masked
echo "PASS: e2e/186 PATA falls back to polling without its interrupt"
