# Lecture 179 - PCI source part 2

Upstream: PeachOS64BitCourse ec753f9.

## What landed

- `pci_cfg_write_dword`: dword config-space write through ECAM
  or legacy IO.
- `pci_cfg_write_word`: word write that reads, masks the right
  half, writes back.
- `pci_size_bars`: probe BAR sizes. Disables IO/MEM/master
  decode during sizing, walks 6 BARs (type 0) or 2 (type 1),
  handles IO BARs, 32-bit memory BARs, and 64-bit BAR pairs
  (the high half is stored as a synthetic IS_EXT BAR), restores
  the command register.

## Upstream bugs preserved

1. `pci_size_bars` calls `pci_cfg_reaD_dword(...)` (capital D
   on the second `D`). The real function is
   `pci_cfg_read_dword`. SamOs adds a static inline shim with
   the typo'd name so the call site stays verbatim and the
   binary semantics match; a code comment documents both.
2. The L178 forward declaration was `pci_size_bar` (singular)
   while the L179 body lands as `pci_size_bars` (plural).
   The orphan forward decl stays in place, suppressed by the
   project's `-Wno-unused-function` build flag.
3. The header-type mask uses `0X7Fu` (capital X). gcc accepts
   both letter cases; preserved verbatim.

## BIOS test status

Source + link. Build links.
