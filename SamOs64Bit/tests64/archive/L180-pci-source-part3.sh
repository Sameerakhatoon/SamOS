#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail(){ echo "FAIL: $*" >&2; exit 1; }

for sym in pci_scan_bus pci_init pci_device_count pci_device_base_class \
           pci_device_subclass pci_device_get pci_enable_upstream_path \
           pci_enable_bus_master bus_enable_bus_master; do
    grep -q "$sym" "$ROOT/src/io/pci.c" || fail "pci.c: $sym missing"
done

# Header: second decl now pci_cfg_write_word.
grep -q '^void.*pci_cfg_write_word' "$ROOT/src/io/pci.h" \
    || fail "pci.h: pci_cfg_write_word decl missing"
# pci.h must no longer have the duplicate dword decl (count only the
# function decl lines, ignoring comments).
count=$(grep -cE '^void\s+pci_cfg_write_dword' "$ROOT/src/io/pci.h")
[ "$count" = "1" ] || fail "pci.h: pci_cfg_write_dword should be declared once (got $count)"

# L180 fixes the pci_cfg_reaD_dword typo at the call site (the
# string remains in comments documenting the L179 history; we
# reject only actual code occurrences).
grep -E '^\s*(uint32_t|static inline uint32_t|szv\s*=|=\s*)?pci_cfg_reaD_dword\s*\(' \
    "$ROOT/src/io/pci.c" \
    && fail "pci.c: pci_cfg_reaD_dword typo still present in code" || true

# kernel calls pci_init.
grep -q 'pci_init()' "$ROOT/src/kernel.c" || fail "kernel.c: pci_init not called"
grep -q '#include "io/pci.h"' "$ROOT/src/kernel.c" || fail "kernel.c: pci.h not included"

# isr80h/window.c follows upstream fix: isr80h_command24_update_window_title.
grep -q 'isr80h_command24_update_window_title' "$ROOT/src/isr80h/window.c" \
    || fail "window.c: L180 isr80h_ rename missing"

# Makefile lists pci.o and a recipe exists.
grep -q 'build/io/pci.o' "$ROOT/Makefile" || fail "Makefile: pci.o missing from FILES"
grep -q '^./build/io/pci.o:' "$ROOT/Makefile" || fail "Makefile: pci.o recipe missing"

( cd "$ROOT" && bash build.sh > /dev/null 2>&1 ) || fail "build.sh failed"
echo "PASS: L180 pci source part 3"
