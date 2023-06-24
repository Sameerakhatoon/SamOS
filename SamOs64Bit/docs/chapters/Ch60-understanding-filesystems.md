# Ch 60 - Understanding file systems (prose)

Book Ch 56: background on what a filesystem actually *is* before we
start writing one in the next few chapters. No source-tree changes.

## The mental model the book sets up

A filesystem is a *map drawn onto the disk's sectors*. The disk is just
an array of 512-byte blocks; the filesystem layers structure on top by
choosing:

1. Which sectors hold metadata (where files live, their names,
   sizes, timestamps, permissions).
2. Which sectors hold the actual file payload.
3. How free space is tracked so allocation knows what to hand out.

That structure is just C structs that happen to be persisted to disk
sectors. Reading a file = read a known metadata sector, find which
data sectors to load, ask the disk driver for them. Writing = inverse,
plus updating the metadata sector(s) to reflect the new layout.

## Why this chapter matters for us

We've spent the last two chapters teaching the kernel to read raw 512-byte
blocks (`disk_read_block`). That's enough to load a hand-crafted boot
sector, but a real OS needs to talk in terms of *files* and *paths*, not
sector numbers. The next chapters introduce FAT16:

- Boot sector + BPB (BIOS Parameter Block) describing the layout
- File Allocation Table chaining clusters into files
- Root directory of fixed-size entries (name, size, start cluster)
- Data region holding cluster payloads

The plan from here:

- Ch 57 - FAT16 layout in detail (prose)
- Ch 58 onwards - implement a path parser, a virtual-filesystem
  dispatch layer, then the FAT16 driver itself

For us specifically the disk image (`bin/os.bin`) will eventually need
to be replaced with a FAT16-formatted image so the kernel has something
to mount. Right now it's just a flat concatenation of `boot.bin`,
`kernel.bin`, and zero-fill. That switchover happens when the first
FAT16 commit lands, not here.
