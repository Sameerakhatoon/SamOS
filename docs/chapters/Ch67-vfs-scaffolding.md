# Ch 67 - VFS scaffolding

Book Ch 63: stand up the indirection layer between the kernel's
`fopen`/`fread`/etc. and any concrete filesystem driver. No FAT16 yet;
just the table + dispatch + descriptor housekeeping.

## What got added

- `src/config.h` - two new caps:
  - `SAMOS_MAX_FILESYSTEMS = 12` - how many distinct drivers can
    register at once.
  - `SAMOS_MAX_FILE_DESCRIPTORS = 512` - how many `fopen`'d files can
    be live at once.
- `src/fs/file.h` - public VFS interface:
  - `FILE_SEEK_MODE` enum (`SEEK_SET`/`CUR`/`END`).
  - `FILE_MODE` enum (`READ`/`WRITE`/`APPEND`/`INVALID`).
  - `struct filesystem { resolve; open; name[20]; }` - the v-table
    each driver fills in.
  - `struct file_descriptor { index; filesystem; private; disk; }` -
    one per `fopen` call.
  - Prototypes for `fs_init`, `fopen`, `fs_insert_filesystem`,
    `fs_resolve`.
- `src/fs/file.c` - bookkeeping for both tables:
  - Two file-scope arrays: `filesystems[SAMOS_MAX_FILESYSTEMS]` and
    `file_descriptors[SAMOS_MAX_FILE_DESCRIPTORS]`. Both start
    zeroed by `fs_init`.
  - `fs_get_free_filesystem()` (static) - first null slot or null.
  - `fs_insert_filesystem(fs)` - puts the driver into a free slot;
    panics-by-halt with a "Problem inserting filesystem" print if the
    table is full (the book's behaviour).
  - `fs_static_load`/`fs_load` - currently no-op; FAT16 will hook in
    here next chapter.
  - `file_new_descriptor(out)` (static) - kzalloc a `file_descriptor`,
    stash it in the first free slot, hand it back. Indices are
    `slot + 1` so descriptor 0 is reserved (matches POSIX-ish
    convention).
  - `file_get_descriptor(fd)` (static) - reverse lookup.
  - `fs_resolve(disk)` - loop the table, call each filesystem's
    `resolve(disk)`, return the first one that returns zero.
  - `fopen(path, mode)` - stub: returns `-EIO`. Real implementation
    lands once we have a driver to dispatch into.
- `src/disk/disk.h` - includes `fs/file.h` and adds
  `struct filesystem* filesystem` to `struct disk`. The pointer is
  filled in by `disk_search_and_init` so any later code can ask "what
  fs is on this drive?" without going back through `fs_resolve`.
- `src/disk/disk.c` - `disk_search_and_init` appended:
  `disk.filesystem = fs_resolve(&disk);`. Today this returns null
  (table is empty); once FAT16 registers it'll point at the FAT16
  `struct filesystem`.
- `src/kernel.c`:
  - Added `#include "fs/file.h"`.
  - `fs_init()` called from `kernel_main` between `kheap_init()` and
    `disk_search_and_init()`. The order matters - `disk_search_and_init`
    calls `fs_resolve` which reads the filesystem table, and the table
    must be zeroed first.
  - Smoke probe under the streamer's print:
    `print(" fopen="); print_hex32((unsigned)fopen("0:/anything", "r"));`
    which produces `fopen=FFFFFFFF` (the cast of `-EIO = -1`).
- `Makefile` - new `./build/fs/file.o` in `FILES` plus its build rule.

## Heap accounting hasn't shifted

`fs_init` zeroes two static arrays; no heap allocs happen during init.
`fopen` short-circuits to `-EIO` before allocating a descriptor. So
the kmalloc smoke probe addresses (`tests/13`) stay at `0x01402000`.

## How test 21 confirms it

`tests/21-vfs-fopen-stub.sh` greps the VGA buffer for
`fopen=FFFFFFFF`. That single string proves:

- `fs_init` ran without halting (otherwise we wouldn't reach the
  fopen print at all).
- `disk_search_and_init`'s `fs_resolve` call returned cleanly
  (otherwise the kernel would have triple-faulted before `print`).
- `fopen` returns the documented stub value; once we replace it with a
  real implementation, this test will need to follow.
