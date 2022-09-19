# Ch 61 - Understanding FAT16 (prose)

Book Ch 57: how FAT16 lays itself out on disk. Pure prose, no code
changes. The next chapters bolt this onto the kernel.

## The four regions, in order

| Region          | Size                                                | Purpose |
| --------------- | --------------------------------------------------- | ------- |
| Boot sector     | 512 bytes (sector 0)                                | Boot code + BIOS Parameter Block |
| Reserved sectors| `reserved_sectors * 512`                            | Usually just the boot sector itself, so 1 |
| FAT 1           | `sectors_per_fat * 512`                             | Cluster chain table |
| FAT 2 (optional)| `sectors_per_fat * 512`                             | Redundant copy of FAT 1 |
| Root directory  | `root_dir_entries * sizeof(fat_directory_item)`     | Fixed-size table of root-level files/dirs |
| Data clusters   | rest of the volume                                  | Actual file payload, in cluster-sized chunks |

So: read sector 0 to learn the BPB, jump past the reserved sectors to
hit FAT 1, jump past both FATs to hit the root dir, jump past the root
dir to hit the data region. Every offset traces back to fields in the
BPB.

## BIOS Parameter Block layout

Lives at offset 0 of the boot sector (the first 3 bytes are a short
jump over the BPB so the CPU doesn't try to execute the data fields as
code). Book's struct:

```c
struct fat_header {
    uint8_t  short_jmp_ins[3];
    uint8_t  oem_identifier[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_copies;
    uint16_t root_dir_entries;
    uint16_t number_of_sectors;
    uint8_t  media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t number_of_heads;
    uint32_t hidden_setors;   // book typo: "setors" - keep verbatim per convention
    uint32_t sectors_big;
} __attribute__((packed));
```

`packed` is non-negotiable: the on-disk layout has no padding.

## The FAT itself

FAT16 = 16-bit entries, one per data cluster.

- Entry value > 0: index of the next cluster in this file's chain
- `0x0000`: free cluster
- `0xFFFF`: end of chain (last cluster of the file)
- Indices 0 and 1 are reserved (so cluster numbers start at 2)

Following a file = look up its starting cluster from its directory
entry, read that cluster, look up the next cluster index from the FAT,
repeat until `0xFFFF`.

## Why two FATs

Redundancy. If FAT 1 is corrupted, the kernel can fall back to FAT 2.
Most images on disk that we'll actually exercise will have 2 copies,
but the BPB's `fat_copies` field is what we'll actually read.

## Knock-on for our boot sector

The boot sector we wrote in Ch 19 (real-mode disk read) and updated in
Ch 22 (protected-mode switch) currently does NOT carry a BPB - bytes
3..61 are our own code. To boot FAT16 we will:

1. Reserve a `short_jmp_ins` plus the full BPB struct's worth of bytes
   at the start of the boot sector.
2. Set those fields to match the disk image we mkfs.fat onto.
3. Move our existing boot code (`org 0`, segment setup, INT 0x13 read)
   after the BPB.

That work happens in the FAT16 implementation commits, not this one.
