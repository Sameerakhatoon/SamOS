# Lecture 77 - GPT partition table reader

**Source commit (PeachOS64BitCourse):** `998f4ee`
**SamOs commit:** L77 on `module1-64bit` branch
**Regression test:** `tests64/L77-gpt-source.sh`

## Why this chapter exists

UEFI disks use GPT (GUID Partition Table) instead of the
legacy MBR. The kernel's existing `disk_search_and_init`
expects an MBR-style FAT BPB at LBA 0; under UEFI that's a
"protective MBR" (a dummy MBR that says "the entire disk is
one partition of type 0xEE") and the real layout is GPT
starting at LBA 1.

L77 adds the source for reading and decoding the GPT header
+ partition entry array. The actual integration (mounting
each partition as a virtual disk) needs `disk_create_new` +
`SAMOS_DISK_TYPE_PARTITION` which L78 brings.

## GPT layout

```
LBA   Content
0     Protective MBR (legacy compat)
1     Primary GPT header
2     First sector of partition entry array
3..   (more entry array)
...
-1    Backup partition entry array
-N    Backup GPT header
```

The header at LBA 1 contains:
- 8-byte "EFI PART" signature
- header size + CRC32
- LBA of THIS header and the BACKUP header
- First / last usable LBAs for data
- Disk GUID
- LBA where the partition entry array begins
- Total entry count + bytes per entry
- CRC32 of the entry array

Each partition entry has:
- Type GUID (= partition role: ESP, Linux Filesystem, etc.)
- Unique GUID (= unique to this partition)
- Starting / ending LBA
- Attribute bits (Required, NoBlockIo, LegacyBoot, ...)
- 72-byte UTF-16LE name

Entries with an all-zero type GUID are unused slots.

## gpt_init walk

```c
int gpt_init() {
    gpt_primary_disk = disk_get(0);
    gpt_partition_table_header_read(&header)
    if (header.signature != "EFI PART") return -EINFORMAT;
    gpt_mount_partitions(&header)
}
```

`gpt_partition_table_header_read` just `disk_read_block`s LBA
1 into a stack sector buffer (C99 VLA, sized at runtime by
`gpt_primary_disk->sector_size`).

`gpt_mount_partitions` uses the disk_streamer to read the
entry array, skips all-zero-GUID slots, and calls
`disk_create_new(SAMOS_DISK_TYPE_PARTITION, ...)` for each
real partition. That call is the L78 dependency.

## Why this is not in the build yet

```c
res = disk_create_new(SAMOS_DISK_TYPE_PARTITION, ...);
```

`disk_create_new` and `SAMOS_DISK_TYPE_PARTITION` don't exist
in our disk.h yet - L78 adds them. The upstream commit
explicitly comments:

> WARNING: CODE WONT COMPILE YET, UNTIL WE IMPLEMENT VIRTUAL
> DISKS IN THE DISK FUNCTIONALITY

So the L77 commit lays down the GPT reader as a source file
without wiring it into the build. We mirror that.

## How CI verifies

Source-only:
- gpt.h has the partition header + entry structs + EFI PART
  signature define
- gpt.c has gpt_init + gpt_mount_partitions
- The rest of the kernel still builds (gpt.c not in FILES)

## Forward look

L78 - virtual disks. `disk_create_new` + SAMOS_DISK_TYPE_
PARTITION land. gpt.c becomes buildable.

L82 - calls gpt_init from somewhere appropriate (probably
disk_search_and_init).
