#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

[ -f "$ROOT/src/io/pci.c" ] || fail "pci.c missing"

for sym in pci_ecam_install_range pci_ecam_init_from_mcfg pci_ecam_available \
           pci_cfg_read_dword pci_cfg_read_word pci_cfg_read_byte \
           pci_cfg_addr_legacy pci_ecam_cfg_ptr acpi_mcfg_t acpi_mcfg_entry_t; do
    grep -q "$sym" "$ROOT/src/io/pci.c" || fail "pci.c: $sym missing"
done

grep -q 'EINVAL' "$ROOT/src/status.h" || fail "status.h: EINVAL missing"
grep -q 'ENOENT' "$ROOT/src/status.h" || fail "status.h: ENOENT missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L178 pci source part 1"
