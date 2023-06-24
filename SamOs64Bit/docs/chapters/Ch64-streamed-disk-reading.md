# Ch 64 - Streamed disk reading

Book Ch 60: hide sector granularity behind a byte-position cursor.
Callers now seek to any byte offset and read any number of bytes; the
streamer handles the sector splits underneath.

## What got added

- `src/disk/streamer.h` - `struct disk_stream { int pos; struct disk* disk; }`
  plus four prototypes (new/seek/read/close).
- `src/disk/streamer.c`:
  - `diskstreamer_new(disk_id)` - looks up the disk handle via
    `disk_get`, returns null if it doesn't exist, otherwise kzalloc's
    a fresh stream sitting at `pos = 0`.
  - `diskstreamer_seek(stream, pos)` - dumb assignment; the next
    `read` figures out the sector + offset.
  - `diskstreamer_read(stream, out, total)` - compute
    `sector = pos / SAMOS_SECTOR_SIZE` and `offset = pos % SECTOR_SIZE`,
    read the whole sector into a stack buffer with `disk_read_block`,
    copy `min(total, SECTOR_SIZE)` bytes from `buf[offset..]` into
    `out`, advance `pos`, then *recurse* with the remainder if
    `total > SECTOR_SIZE`. Recursion lets a 4 KiB read transparently
    span 8 sectors with no caller-side loop.
  - `diskstreamer_close(stream)` - just `kfree(stream)`.
- `Makefile` - new `./build/disk/streamer.o` in `FILES` plus its build
  rule. Same `-I./src/disk` as `disk.o`.
- `src/kernel.c`:
  - Removed the Ch 59 path-parser smoke probe (`pp_drv=...`). The
    parser itself is still in the tree and still linked; we just lost
    the on-screen verification line. `tests/18-path-parser.sh` deleted
    in the same commit because the line it grepped for is gone (the
    Ch 59 commit still ships the test in git history).
  - Added the streamer smoke probe: `diskstreamer_new(0)`,
    `seek(0x1FE)`, `read(2 bytes)`, `close()`, then
    `print("ss="); print_hex32((ss[0]<<8)|ss[1]);`. Output:
    `ss=000055AA`.

## Why 0x1FE

Byte 0x1FE is offset 510 inside sector 0, where every BIOS-bootable
disk holds `0x55 0xAA`. Test 16 already proves that sector 0's bytes
510..511 are the signature via raw `disk_read_block`. Test 19 proves
the same bytes are reachable through the streamer's
position-to-sector arithmetic - so a regression that mangles `pos /
SECTOR_SIZE`, `pos % SECTOR_SIZE`, or the `buf[offset+i]` copy will
fail this test while leaving test 16 untouched.

## Heap accounting

The streamer alloc + close pair leaves a net-zero block delta by the
time the kmalloc smoke probe runs, because `kfree` releases the block
for reuse before the next alloc happens. So `tests/13-kmalloc-works.sh`
goes back to expecting `km1 = 0x01402000` (1025 paging blocks consumed
before the probe, none from the streamer because it's closed first).

## Notes for next chapter

The streamer's recursion uses `disk_read_block` for every sector,
which means a multi-sector read pays the full poll-DRQ-and-load-256-
words cost per sector with no batching. That's fine for FAT16's small
metadata reads; we'll revisit when we get to multi-sector file
payload reads.
