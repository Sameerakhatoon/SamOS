# Ch 70 - FAT16 structures and constants

Book Ch 66: define the C structs that mirror what FAT16 looks like on
disk, plus the constants needed to interpret them. Pure type
definitions; no behaviour change yet.

## What got added (all inside src/fs/fat/fat16.c)

### Constants

- `SAMOS_FAT16_SIGNATURE = 0x29` - the value the EBPB's `signature`
  field must hold for the volume to be claimed as FAT16.
- `SAMOS_FAT16_FAT_ENTRY_SIZE = 0x02` - each FAT entry is 2 bytes
  (this is FAT16 not FAT12 or FAT32).
- `SAMOS_FAT16_BAD_SECTOR = 0xFF7` - marker for unusable clusters.
- `SAMOS_FAT16_UNUSED = 0x00` - free / unused entry marker.
- `FAT_ITEM_TYPE` enum-style typedef with `FAT_ITEM_TYPE_DIRECTORY=0`
  and `FAT_ITEM_TYPE_FILE=1`.
- `FAT_FILE_*` bitmask values for `fat_directory_item.attribute`:
  read-only / hidden / system / volume label / subdirectory / archived
  / device / reserved.

### Structs (all `__attribute__((packed))` where they mirror disk bytes)

- `fat_header_extended` (26 bytes) - the EBPB fields: drive number,
  WinNT bit, signature, 32-bit volume id, 11-byte volume id string,
  8-byte system id string.
- `fat_header` (36 bytes) - the BPB: 3-byte short-jmp, 8-byte OEM,
  bytes/sector, sectors/cluster, reserved-sectors, fat-copies,
  root-dir-entries, number-of-sectors (16-bit), media-type,
  sectors-per-fat, sectors-per-track, number-of-heads, hidden-setors
  (book's misspelling preserved per project convention), sectors_big.
- `fat_h` - container that bundles `primary_header` with a union
  `shared` whose only current member is `extended_header`. Wrapping
  the extended header in a union leaves room to swap in a FAT32-style
  EBPB later without redesigning the API.
- `fat_directory_item` (32 bytes) - one row from a FAT16 directory:
  8-byte filename, 3-byte ext, attribute, reserved, creation tenths,
  creation time, creation date, last access, high-16 of first cluster,
  last-mod time, last-mod date, low-16 of first cluster, file size.
- `fat_directory` - in-memory representation of a directory's parsed
  contents: pointer to a `fat_directory_item` array plus a count and
  the disk sector range it lives in.
- `fat_item` - tagged union: either a `fat_directory_item*` (a file)
  or a `fat_directory*` (a directory), with `FAT_ITEM_TYPE type`
  saying which.
- `fat_item_descriptor` - one open handle: a `fat_item*` plus a
  32-bit position cursor for streaming reads.
- `fat_private` - the per-mount blob that goes into the VFS
  `file_descriptor.private` slot: full parsed `fat_h`, the root
  directory, and three pre-allocated `disk_stream*`s (cluster reader,
  FAT reader, directory reader) so we don't keep newing/freeing
  streamers on every read.

### One self-check

`fat16_check_sizes()` returns 1 iff the three on-disk struct sizes
match what the spec requires:

- `sizeof(fat_header) == 36`
- `sizeof(fat_header_extended) == 26`
- `sizeof(fat_directory_item) == 32`

If a future edit accidentally drops `__attribute__((packed))` or
reorders a 16-bit field next to an 8-bit one and gcc inserts padding,
this catches it before any disk-shaped logic gets a chance to
mis-decode.

## Smoke probe

`kernel_main` prints `fszs=` followed by `fat16_check_sizes()` as
hex. Expected on-screen `fszs=00000001`.
`tests/24-fat16-struct-sizes.sh` greps for that exact line.

## Heap accounting

No new allocs in this commit; the structs are types only, not
instances. So `tests/13` expected addresses keep their Ch 60 values
(`km1 = 0x01402000`).
