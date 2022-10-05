# Ch 72 - FAT16 resolver and root directory

Book Ch 68: the FAT16 driver finally does real work. Resolving a disk
now means reading its BPB, checking the EBPB signature, and walking
the root directory off the volume.

## What got added

- `src/status.h` - `EFSNOTUS = 5`. Returned by `fat16_resolve` when
  the EBPB signature byte isn't `0x29`, so the VFS layer can keep
  trying other registered drivers instead of binding this disk.
- `src/fs/fat/fat16.c` - five new helpers and a real resolver:
  - Includes `memory/heap/kheap.h` and `memory/memory.h`.
  - `fat16_init_private(disk, private)` - zero the blob, then
    `diskstreamer_new(disk->id)` three times to populate the
    cluster/FAT/directory stream pointers. The streams are owned by
    the driver for the disk's lifetime.
  - `fat16_sector_to_absolute(disk, sector)` - just
    `sector * disk->sector_size`. Trivial but factored out because
    every disk operation needs the conversion.
  - `fat16_get_total_items_for_directory(disk, dir_start_sector)` -
    streams `fat_directory_item`s starting at the given sector. Stops
    on `filename[0] == 0x00` (end-of-list marker), skips
    `filename[0] == 0xE5` (deleted entries), counts everything else.
    Returns the count or `-EIO` if the stream fails.
  - `fat16_get_root_directory(disk, fat_private, directory)` -
    computes root_dir_sector = `fat_copies * sectors_per_fat + reserved_sectors`,
    grabs the entry count from the BPB, kzalloc's a big-enough buffer,
    streams the whole root directory in, fills in the `fat_directory`
    struct (item, total, sector_pos, ending_sector_pos).
  - `fat16_resolve(disk)` - kzalloc a `fat_private`, plug it into
    `disk->fs_private` and `disk->filesystem`, open a temp stream,
    read the full `fat_h` header off sector 0, verify the EBPB
    signature is `0x29`, walk the root directory. On any error roll
    back: close the temp stream, kfree the private blob, null out
    `disk->fs_private`, return the error.
- `src/fs/fat/fat16.h` - exposes
  `int fat16_root_dir_total(struct disk* disk)` so the smoke probe
  in kernel.c can read the parsed root-directory count without
  knowing about `struct fat_private`.

## Smoke probes

Two new prints in `kernel_main`, replacing the Ch 67 `priv=` line:

```c
print(" pnz=");
print_hex32(disk_get(0)->fs_private != 0 ? 1 : 0);
print(" rdt=");
print_hex32(fat16_root_dir_total(disk_get(0)));
```

Expected output: `pnz=00000001 rdt=00000000`.

- `pnz=00000001` proves the resolver didn't roll back. It would be 0
  if the signature check or root-directory walk failed, because the
  cleanup branch nulls `fs_private` on `res < 0`.
- `rdt=00000000` proves the directory walk succeeded and (correctly)
  found zero entries. The 16 MiB FAT volume from Ch 61 is zero-filled
  outside the boot sector, so the root-directory's first
  `fat_directory_item` has `filename[0] == 0x00` and the counter
  short-circuits to 0.

## Test 25 relaxed, test 26 added

Ch 67's `tests/25-disk-id-and-fs-private.sh` asserted `priv=00000000`.
That stops being true the moment a resolver runs. The test is now
trimmed to just `did=00000000`. `tests/26-fat16-resolver.sh` covers
the new state (`pnz=00000001` and `rdt=00000000`).

## Heap accounting knock-on

`fat16_resolve` is the first thing on the boot path that does
substantial heap work:

- 1 block for `fat_private`
- 3 blocks for the cluster / FAT / directory streamers
- 1 block for the 2 KiB root-directory buffer
  (`root_dir_entries * sizeof(fat_directory_item) = 64 * 32 = 2048`)
- 1 block for the temp stream used to read the header, freed at the
  end of resolve

So when the Ch 48 kmalloc smoke probe runs, the heap's block table is
1031 entries deep, with slot 1031 (the temp stream) freed but never
compacted away. First-fit picks slot 1031 for `km1(100)`, so the
addresses move from `0x01402000`/`0x01403000`/`0x01402000` to
`0x01407000`/`0x01408000`/`0x01407000`. `tests/13` updated with the
new comment + values.

## Why we don't need hello.txt yet

The resolver only cares about counting entries until it hits the
`filename[0] == 0x00` terminator. With an empty volume that happens on
the first entry, total=0, and resolve returns clean. When we later
mcopy `hello.txt` into the volume the first entry will have a real
filename byte and `rdt` will become 1; that's an expected change for a
future chapter's test, not a regression here.
