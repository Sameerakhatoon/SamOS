# Lecture 184 - disk-driver header + source

Upstream: PeachOS64BitCourse 4a9f9d8 + 0963971 (squashed).

Upstream splits the disk-driver abstraction across two commits
that both carry the same lecture number. SamOs squashes them so
the kernel still builds after a single commit.

## What landed

`src/disk/driver.h`:

- `struct disk_driver` with a `name`, a `functions` sub-struct
  (loaded, unloaded, mount, unmount, read, write,
  mount_partition), and an opaque `private` pointer.
- Six function-pointer typedefs (`DISK_DRIVER_*`).
- Public API: `disk_driver_register`,
  `disk_driver_registered`, `disk_driver_private_data`,
  `disk_driver_mount_partition`, `disk_driver_mount_all`,
  `disk_driver_system_init`,
  `disk_driver_system_load_drivers`.

`src/disk/driver.c`:

- `disk_driver_vec` global vector.
- `disk_driver_system_init`: allocate the vector, call the load
  stub.
- `disk_driver_system_load_drivers`: empty stub. L189/L195 wire
  in PATA and NVME.
- `disk_driver_mount_partition`: validates that the disk's
  driver matches and the disk is REAL, then forwards to the
  driver vtable.
- `disk_driver_registered`: linear-scan by name.
- `disk_driver_register`: push, call `loaded`, rollback on
  failure.
- `disk_driver_mount_all`: walk the vector and call mount on
  each driver.

`struct disk` gains a `driver` pointer; `disk.h` includes
`driver.h`. `Makefile` adds `driver.o` to FILES and a recipe.

## Upstream bug preserved

`disk_driver_register` calls `disk_driver_register(driver)` in
its uniqueness check, where the intent was clearly
`disk_driver_registered(driver)`. The function infinite-recurses
on the first invocation and would blow the stack. Preserved
verbatim with a comment because no caller in L184 actually
invokes it (the load helper is an empty stub); the bug stays
dormant until a future fix lecture or wire-up.

## BIOS test status

Source + link. Build links.
