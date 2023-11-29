# Lecture 189 - finalize and test PATA driver

Upstream: PeachOS64BitCourse af3211e.

## What landed

- `disk_search_and_init` switches return type to `int`.
  Body becomes: `disk_driver_system_init` -> allocate
  `disk_vector` -> `disk_mount_all` (which calls every
  registered driver's mount). Legacy manual `disk_create_new`
  for disk0 is gone; PATA's mount handler now creates the REAL
  disk via `disk_create_new` on its own.
- `disk_mount_all` is the new public helper.
- `disk_read_sector` body is commented out upstream; SamOs
  leaves the body intact so the L186 deviation fallback in
  `disk_read_block` keeps the legacy ATA path working for
  driverless disks (e.g. partition disks created by GPT before
  the partition driver attaches). The function is still
  unreferenced by the driver path.
- `disk_driver_register` typo (recursive call vs
  `disk_driver_registered`) is fixed.
- `disk_private_data_driver` lands here upstream; SamOs already
  added it at L188 so this is a no-op.

## SamOs deviation

After PATA mounts disk0 we cache `disk_primary_handle` to the
new disk so the existing `disk_primary()` accessor keeps
returning the same disk shape as before the driver split.

## BIOS test status

Source + link. Build links.
