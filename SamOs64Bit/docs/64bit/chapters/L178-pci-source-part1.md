# Lecture 178 - PCI source part 1

Upstream: PeachOS64BitCourse 57bbfcc.

## What landed

`src/io/pci.c` is created with:

- ECAM range table (`g_ecam`, `g_ecam_count`, `g_ecam_enabled`)
  and its install API (`pci_ecam_install_range`).
- ACPI MCFG parser (`pci_ecam_init_from_mcfg`) that walks the
  `acpi_mcfg_entry_t` array and asks a caller-supplied mapper
  to translate phys to virt, then installs each range.
- Legacy 0xCF8/0xCFC config-space address builder
  (`pci_cfg_addr_legacy`).
- `pci_cfg_read_byte`, `_word`, `_dword` that prefer ECAM and
  fall back to legacy port IO.
- Forward decls for `pci_scan_bus` and `pci_size_bar` that L179
  will implement.

`src/status.h` gains `EINVAL` and `ENOENT` codes.

## Upstream bug preserved

`pci_ecam_cfg_ptr`'s range-match has the second comparison
wrong: `bus < r->end_bus` should be `bus > r->end_bus`. As
written every ECAM range fails the check and the function
always returns NULL, silently forcing every config access onto
the legacy port path. SamOs preserves the bug verbatim and
documents it with a comment so the audit trail records why the
ECAM path stays dormant.

L180 will add `pci.o` to the Makefile FILES list.

## BIOS test status

Source + link. Build links.
