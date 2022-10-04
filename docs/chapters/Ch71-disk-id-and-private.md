# Ch 71 - Disk id + fs_private

Book Ch 67: tiny but necessary. Each disk gets a stable integer id
(streamers will use this) and an opaque `void* fs_private` for
filesystem-specific state (FAT16's `fat_private` blob).

## What got added

- `src/disk/disk.h` - two new fields on `struct disk`:
  - `int id` - the disk's stable index. Currently zero because we
    only support one disk; later this will be the index used by
    `disk_get(id)` and passed to `diskstreamer_new(id)`.
  - `void* fs_private` - opaque per-mount blob. Filesystem drivers
    will allocate their own private struct (FAT16 plans to use
    `struct fat_private`) and store the pointer here during their
    `resolve()` call, then retrieve it in `open()` / `read()`.
- `src/disk/disk.c` - one new line in `disk_search_and_init`:
  `disk.id = 0;` (the `memset` already zero-filled `fs_private`, so
  there's no second line for it).
- `src/kernel.c` - smoke probe under the Ch 66 line:
  ```c
  print(" did="); print_hex32(disk_get(0)->id);
  print(" priv="); print_hex32((uintptr_t)disk_get(0)->fs_private);
  ```
  Output: `did=00000000 priv=00000000`.

## Why the void* not a typed pointer

The book uses `void*` so the disk struct doesn't have to know about
every filesystem type the kernel might link. Each driver casts the
pointer to its own private struct when it pulls the value back out.
We follow the book convention exactly.

## How test 25 confirms it

`tests/25-disk-id-and-fs-private.sh` greps for `did=00000000` and
`priv=00000000`. The id check proves the new field exists and was set
to the documented value; the priv check proves the field exists *and*
that no filesystem driver has secretly started populating it.
