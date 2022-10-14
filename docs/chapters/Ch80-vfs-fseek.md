# Ch 80 - VFS fseek

Book Ch 76: add another dispatch slot to the filesystem v-table. No
visible behaviour change yet (FAT16's `seek` lands next chapter).

## What got added

- `src/fs/file.h`
  - `typedef int (*FS_SEEK_FUNCTION)(void* private, uint32_t offset,
    FILE_SEEK_MODE seek_mode);`
  - `struct filesystem` gains a `FS_SEEK_FUNCTION seek;` between
    `read` and `name`. The FAT16 driver's existing designated
    initializer leaves `.seek = NULL` until Ch 77.
  - `int fseek(int fd, int offset, FILE_SEEK_MODE whence);` prototype.
- `src/fs/file.c` - real `fseek`:
  - `file_get_descriptor(fd)`, return `-EIO` on null.
  - Dispatch to `desc->filesystem->seek(desc->private, offset,
    whence)`.

## No new smoke probe

Calling `fseek` today would dereference `disk->filesystem->seek`
which is still NULL for FAT16. Without a `seek` implementation we'd
crash. So this commit is pure plumbing - the kernel doesn't exercise
`fseek` until Ch 77 lands. Existing 24 tests still pass because no
probe calls it.

## Why a separate function-pointer slot

Could have piggy-backed `fseek` onto the file descriptor's
`private` field directly, but keeping it on the filesystem struct
mirrors the `open`/`read` split and lets a future ramfs driver
implement its own `seek` semantics (e.g. zero-cost seek into a
contiguous buffer) without touching the VFS layer.
