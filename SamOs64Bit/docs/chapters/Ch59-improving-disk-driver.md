# Ch 59 - Improving the disk driver

Book Ch 55: wrap the raw `disk_read_sector` from last chapter in a thin
abstraction so the rest of the kernel can talk to disks via a handle
rather than a bare LBA-and-port routine. The change is small but it
puts the seams in place for adding secondary drives, virtual disks,
and filesystem mounting later.

## What changed

- `src/config.h` - added `SAMOS_SECTOR_SIZE 512`. The disk struct uses
  this so we can hand a sector size to filesystem code without
  hard-coding 512 everywhere.
- `src/disk/disk.h`
  - Dropped the `disk_read_sector` prototype. The function still exists
    but is now `static` inside `disk.c`; callers must go through
    `disk_read_block`.
  - Added `typedef unsigned int SAMOS_DISK_TYPE;` and one constant
    `SAMOS_DISK_TYPE_REAL = 0`. (Project prefix; book uses `PEACHOS_`.)
  - Added `struct disk { SAMOS_DISK_TYPE type; int sector_size; };`.
  - Added prototypes `disk_search_and_init`, `disk_get(int index)`,
    `disk_read_block(struct disk*, lba, total, buf)`.
- `src/disk/disk.c`
  - Pulled in `config.h`, `status.h`, `memory/memory.h`.
  - Added file-scope `struct disk disk;` to represent the one primary
    drive we support today.
  - `disk_search_and_init()` zeros it and sets `type` + `sector_size`.
    Right now it's a one-disk init; the name leaves room to grow into
    a real PCI scan.
  - `disk_get(int index)` returns `&disk` for index 0 and `0` otherwise.
  - `disk_read_block` rejects any handle that isn't our one disk with
    `-EIO`, then defers to the static `disk_read_sector`.
  - Forward-declares `disk_read_sector` at the top of the file (project
    style: every function forward-declared at the top of its .c).
- `src/kernel.c`
  - `kheap_init()` is now followed by `disk_search_and_init()` so the
    primary disk handle exists before anything else needs it.
  - The bootsig smoke probe was switched from
    `disk_read_sector(0, 1, buf)` to
    `disk_read_block(disk_get(0), 0, 1, buf)`. Same on-screen output
    (`bootsig=000055AA`), so `tests/16-disk-reads-sector.sh` continues
    to assert the read path end-to-end.

## Why keep the smoke probe

The book deletes the smoke probe in this chapter (`char buf[512];
disk_read_sector(0, 1, buf);`). We retarget it to the new abstraction
instead of removing it, because dropping it would also remove the
on-screen `bootsig=000055AA` line that test 16 greps for. Keeping the
probe means a regression in either layer (`disk_read_block`'s
handle check, or the underlying PIO loop) gets caught immediately.
