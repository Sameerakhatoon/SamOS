# Ch 65 - Incorporating FAT16 BPB into the bootloader

Book Ch 61: turn `bin/os.bin` into a FAT16-shaped volume. Two pieces:
the boot sector grows a BIOS Parameter Block (BPB) describing the
layout, and the image gets padded to 16 MiB so there's actually room
for FAT structures + data clusters.

## What got added

- `src/boot/boot.asm` - the placeholder `times 33 db 0` at offset 3 is
  replaced with the FAT16 BPB (33 bytes) + DOS 4.0 Extended BPB (26
  bytes). Total 59 bytes of header, so `start:` now sits at offset 62.
  Values match the book's chapter exactly, except project-renamed
  identifiers ("SAMOS   " for OEM, "SAMOS BOO  " for volume ID,
  "FAT16   " for system ID).

  Layout summary (offset : size : meaning):
  - 0-2 : 3 : `jmp short start; nop`
  - 3-10 : 8 : OEM identifier
  - 11-12 : 2 : `BytesPerSector = 0x0200`
  - 13 : 1 : `SectorsPerCluster = 0x80`
  - 14-15 : 2 : `ReservedSectors = 200`
  - 16 : 1 : `FATCopies = 2`
  - 17-18 : 2 : `RootDirEntries = 0x40`
  - 19-20 : 2 : `NumSectors = 0` (use 32-bit field instead)
  - 21 : 1 : `MediaType = 0xF8` (fixed disk)
  - 22-23 : 2 : `SectorsPerFat = 0x100`
  - 24-25 : 2 : `SectorsPerTrack = 0x20`
  - 26-27 : 2 : `NumberOfHeads = 0x40`
  - 28-31 : 4 : `HiddenSectors = 0`
  - 32-35 : 4 : `SectorsBig = 0x773594`
  - 36 : 1 : `DriveNumber = 0x80`
  - 37 : 1 : `WinNTBit = 0`
  - 38 : 1 : `Signature = 0x29` (extended BPB present)
  - 39-42 : 4 : `VolumeID = 0xD105`
  - 43-53 : 11 : `VolumeIDString = "SAMOS BOO  "`
  - 54-61 : 8 : `SystemIDString = "FAT16   "`
  - 62-509 : code (segment setup, GDT, ATA PIO read, far jump)
  - 510-511 : `0x55 0xAA` boot signature
- `Makefile` - the trailing pad changed from
  `dd /dev/zero bs=512 count=100` (50 KiB of zeros) to
  `dd /dev/zero bs=1048576 count=16` (16 MiB of zeros). With this
  padding the image is large enough that the BPB's region offsets
  (reserved sectors -> FAT 1 -> FAT 2 -> root dir -> data clusters)
  all land inside the file rather than off the end.
- `hello.txt` - one-line text file at repo root, contents
  `Hello world!\n`. Not yet *copied into* the FAT16 volume because the
  next step is a tooling decision (see below); the file is parked here
  so the Ch 61 plumbing is self-contained and ready.

## Deferred: writing files into the volume

The book uses `sudo mount -t vfat ./bin/os.bin /mnt/d && cp` to drop
`hello.txt` into the FAT16 volume. Two reasons we hold off:

1. `/mnt/d` on WSL Ubuntu is the Windows D: drive; doing what the book
   literally says would overwrite the host filesystem.
2. Mounting via the kernel needs a properly formatted FAT (zero-filled
   FAT regions are *invalid*, not "empty file table"). `mkfs.fat`
   would overwrite our hand-written BPB, defeating the whole point of
   this chapter.

`mtools` (installed: `mformat -k`, `mcopy -i`) can lay down the FAT
tables and root-dir entries without touching the boot sector. The
sensible time to introduce that step is when the kernel-side FAT16
driver actually needs to read a file - at which point the test can
write `hello.txt` into the image with `mcopy` and the driver can read
it back. That'll happen alongside the FAT16 driver commits in Ch 63+.

For now the test does *not* check for `hello.txt` inside the volume,
because nothing in the kernel reads it yet.

## How test 20 confirms the layout

`tests/20-fat16-bpb.sh` checks the on-disk bytes of `bin/os.bin`:

- OEM identifier (bytes 3..10) = "SAMOS   "
- `BytesPerSector` (bytes 11..12) = `0x0200` little-endian
- `SectorsPerCluster` (byte 13) = `0x80`
- `SystemIDString` (bytes 54..61) = "FAT16   "
- Boot signature (bytes 510..511) = `0x55 0xAA`
- Image size > 16 MiB + 512 + 16 KiB

These five checks cover every field a regression would care about: a
broken `db` width or `dw`/`dd` mix would shift one of them, a
miscounted `times` directive would push the signature off 510, and a
broken Makefile pad would leave the volume too small for the FAT
regions.
