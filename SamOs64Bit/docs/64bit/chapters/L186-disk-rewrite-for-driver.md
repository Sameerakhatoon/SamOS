# Lecture 186 - rewrite disk code for driver abstraction

Upstream: PeachOS64BitCourse a05c1c4.

## What landed

`src/disk/disk.c`:

- `disk_create_partition`: thin wrapper that calls
  `disk_driver_mount_partition` on the disk's driver.
- `disk_filesystem_mount`: extracted out of `disk_create_new`.
  Calls `fs_resolve` and matches the volume label against the
  kernel's expected name to set `primary_fs_disk`.
- `disk_create_new` now stores `driver` on the disk and no
  longer auto-mounts the filesystem.
- `disk_read_block` now dispatches reads through
  `idisk->driver->functions.read` and returns `-EIO` when the
  driver does not advertise a read op.

`src/disk/disk.h` exports `disk_create_partition` and
`disk_filesystem_mount`.

`src/disk/gpt.c` upstream switches to
`disk_create_partition` + `disk_filesystem_mount`.

## SamOs deviation

Upstream's L186 leaves the bootstrap disk0 without a driver. The
PATA driver lands at L189; between L186 and L189 every read
through `disk_read_block` is `idisk->driver->functions.read`
where `idisk->driver` is NULL, which crashes early boot before
the user can even see the count from L181.

SamOs keeps the kernel bootable by:

- `disk_create_new` still calls `disk_filesystem_mount` inline
  when `driver` is NULL, so the bootstrap disk0 still resolves
  its filesystem.
- `disk_read_block` falls back to the legacy
  `disk_read_sector` path when `idisk->driver` is NULL.

Both deviations carry inline comments. When PATA arrives at
L189 the fallback paths fall idle (the bootstrap disk gets a
real driver), and our copy converges with upstream's
post-L189 behaviour.

`gpt.c` stays on `disk_create_new` (with the L183-widened
signature) because the GPT partitions inherit disk0's NULL
driver; `disk_create_partition` would route through
`disk_driver_mount_partition` and bounce on the NULL driver
check. A later lecture (or fix commit) once PATA is registered
can move gpt.c onto `disk_create_partition`.

## BIOS test status

Source + link. Build links.
