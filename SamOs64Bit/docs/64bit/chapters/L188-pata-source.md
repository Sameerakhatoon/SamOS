# Lecture 188 - PATA disk driver source

Upstream: PeachOS64BitCourse e0dbb0f.

## What landed

`src/disk/drivers/pata.c` implements:

- `pata_pci_device_present`: scan the PCI device vector for an
  IDE controller (class 0x01 / subclass 0x01).
- `pata_private_new`: kzalloc the per-disk private data.
- `pata_disk_base_drive_address` / `pata_disk_ctrl_drive_address`:
  resolve the base / control IO port from the private data
  channel.
- `pata_disk_read_sector`: classic PIO read loop. Wait for not
  busy, write LBA + sector count, issue READ SECTORS (0x20),
  wait for DRQ, drain 256 words per sector.
- `pata_driver_read`: read via the hardware disk.
- `pata_driver_write`: stub returning `-EUNIMP`.
- `pata_driver_mount`: probe PCI, allocate a primary-drive
  private record, create a REAL disk through `disk_create_new`.
- `pata_driver_unmount`: kfree the private record.
- `pata_driver_mount_partition`: allocate a PARTITION-typed
  private record, create a partition disk through
  `disk_create_new`.
- `pata_driver` vtable + `pata_driver_init` accessor.

`src/disk/driver.c` calls `disk_driver_register(pata_driver_init())`
in `disk_driver_system_load_drivers`.

## SamOs deviations from upstream

1. Upstream's L188 commits `pata_disk_ctrl_drive_address(struct
   disk disk, ...)` (by value). The body then passes `disk` to
   `disk_private_data_driver` which expects a pointer. The file
   would not compile. L189 fixes the arg to `struct disk*`;
   SamOs lands that fix at L188 so the build stays green.
2. `disk_private_data_driver` is declared and implemented at
   L188 (upstream lands its decl at L189). Without it the L188
   pata source would not link.
3. Identifier `disk_private_data_driver` (note the noun-order
   inversion vs `disk_driver_private_data`) is preserved
   verbatim.

## Upstream bug preserved

`pata_private_new` ignores its `type` parameter and always
writes `PATA_PRIMARY_DRIVE`. PARTITION-typed allocations end
up labeled as PRIMARY. Preserved verbatim with a code comment.

## BIOS test status

Source + link. Build links.
