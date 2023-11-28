# Lecture 187 - PATA disk driver header

Upstream: PeachOS64BitCourse 7a7b939.

A header-only commit that lands `src/disk/drivers/pata.h`:

- PCI class/subclass constants for IDE controllers.
- Primary/secondary base + control IO ports.
- Drive enum: `PATA_PRIMARY_DRIVE`, `PATA_SECONDARY_DRIVE`,
  `PATA_PARTITION_DRIVE`, plus the `PATA_INVALID_DRIVE`
  sentinel.
- `struct pata_driver_private_data` (per-disk private):
  the `PATA_DISK_DRIVE` channel plus a back-pointer to the
  REAL disk so partition disks can resolve their backing.
- `pata_driver_init` forward decl.

L188 lands the source.

## BIOS test status

Source + link. Build links.
