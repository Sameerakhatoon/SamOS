#!/usr/bin/env bash
# e2e/176-apic-pc.sh - the same on pc (PIIX, IDE disk).
. "$(dirname "$0")/_lib.sh"
. "$(dirname "$0")/_shell_lib.sh"
. "$(dirname "$0")/_apic_lib.sh"
E2E_MACHINE=pc
apic_check
echo "PASS: e2e/176 apic pc"
