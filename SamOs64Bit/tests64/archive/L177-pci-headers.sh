#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

[ -f "$ROOT/src/io/pci.h" ] || fail "pci.h missing"

for sym in PCI_HEADER_VENDOR_OFFSET PCI_HEADER_BAR5_OFFSET PCI_CFG_ADDRESS PCI_DATA_ADDRESS \
           'struct pci_device_bar' 'struct pci_address' 'struct pci_device' \
           'struct pci_ecam_range' \
           pci_ecam_init_from_mcfg pci_ecam_install_range pci_ecam_available \
           pci_device_count pci_cfg_read_byte pci_cfg_read_word pci_cfg_read_dword \
           pci_init pci_device_get pci_device_base_class pci_device_subclass \
           bus_enable_bus_master; do
    grep -q "$sym" "$ROOT/src/io/pci.h" || fail "pci.h: $sym missing"
done

# Preserve the upstream double-decl of pci_cfg_write_dword.
count=$(grep -c 'pci_cfg_write_dword' "$ROOT/src/io/pci.h")
[ "$count" -ge 2 ] || fail "pci.h: pci_cfg_write_dword should be declared twice (upstream typo)"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L177 pci headers"
