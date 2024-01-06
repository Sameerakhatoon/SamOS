#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

for sym in pci_cfg_write_dword pci_cfg_write_word pci_size_bars; do
    grep -q "$sym" "$ROOT/src/io/pci.c" || fail "pci.c: $sym missing"
done

# Preserve upstream pci_cfg_reaD_dword typo.
grep -q 'pci_cfg_reaD_dword' "$ROOT/src/io/pci.c" \
    || fail "pci.c: upstream pci_cfg_reaD_dword typo not preserved"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L179 pci source part 2"
