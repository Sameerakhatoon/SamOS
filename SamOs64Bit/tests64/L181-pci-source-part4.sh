#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -q 'Total PCI devices:' "$ROOT/src/kernel.c" || fail "kernel.c: PCI count print missing"
grep -q 'pci_device_count()' "$ROOT/src/kernel.c" || fail "kernel.c: pci_device_count call missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L181 pci source part 4"
