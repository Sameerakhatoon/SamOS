# Ch 78 - Refining the FAT16 initialization

Ch 74 is a single-line correction to the
`fat16_read_internal_from_stream` function that landed in Ch 72:

```c
// Wrong
int starting_pos = (starting_sector * disk->sector_size) * offset_from_cluster;

// Right
int starting_pos = (starting_sector * disk->sector_size) + offset_from_cluster;
```

Multiplying by `offset_from_cluster` would land the disk stream
anywhere except the sector we wanted, breaking every subsequent read.

## Our position

We never shipped the `*` form. The Ch 72 implementation in this repo
already uses `+`, because the bug would have broken the very next
chapter's smoke probe (`fread` of `hello.txt`). Code-wise this chapter
is a no-op for us.

This note exists so the chapter-to-doc trail stays continuous: every
chapter corresponds to either a code commit or a "no-op
acknowledgment" note like this one.
