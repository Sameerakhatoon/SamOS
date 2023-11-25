# Lecture 183 - attaching hardware to disk + driver metadata

Upstream: PeachOS64BitCourse 36f4674.

## What landed

`struct disk` gains two pointers:

- `hardware_disk`: REAL disks point at themselves; partition
  disks point at the REAL backing disk.
- `driver_private`: opaque per-disk pointer for the driver
  (PATA per-channel state in L188, NVME queue in L194).

`disk_create_new` widens to take a `disk_driver*`, the
`hardware_disk`, and a `driver_private_data` pointer. The body
enforces:

- type=REAL must not pass a hardware_disk;
- type=REAL forces hardware_disk to point at the new disk;
- non-REAL disks must pass a non-NULL hardware_disk;
- the supplied hardware_disk must itself be REAL.

`disk_hardware_disk` is the trivial accessor.

## SamOs caller updates

- `disk_search_and_init`: `disk_create_new(NULL, NULL,
  SAMOS_DISK_TYPE_REAL, 0, 0, sector_size, NULL, &handle)`.
- `gpt.c` partition creation: `disk_create_new(NULL,
  gpt_primary_disk, SAMOS_DISK_TYPE_PARTITION, start, end,
  sector_size, NULL, NULL)`.

The `struct disk_driver` itself is just a forward decl here;
L184 lands the real type.

## BIOS test status

Source + link. Build links.
