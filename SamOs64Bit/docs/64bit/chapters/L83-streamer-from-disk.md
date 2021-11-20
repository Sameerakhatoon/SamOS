# Lecture 83 - improving the disk streamer

L83 adds `diskstreamer_new_from_disk(struct disk*)` and switches
the FAT16 driver to use it. The motivation is L78/L82: after GPT
parsing, partitions become VIRTUAL disks. Their `id` is not
necessarily what `disk_get(i)` would return for some `i`; the
disks vector is indexed by insertion order, and the virtual-disk
ids are minted independently. So FAT16's `init_private` could not
roundtrip a partition disk through `diskstreamer_new(disk->id)`
reliably.

The new constructor just takes the already-resolved pointer and
skips the lookup. The body is identical to `diskstreamer_new` past
the `disk_get` line.

## SamOs deviation: the streamer read stays iterative

Upstream's L83 commit also rewrites `diskstreamer_read` from the
iterative form (a while loop that walks one sector at a time) back
into a RECURSIVE form (read up to the sector boundary, then tail-
call itself for the remainder). We do not adopt that change.

SamOs went iterative in L64 with a specific note: the recursive
form burned one stack frame per sector boundary crossed, so a
4 KiB read costs 8 frames and a 1 MiB read costs 2048. That is a
real kernel-stack risk - the kernel stack on SamOs is only 1 MiB
and it is shared with interrupts. The iterative loop produces the
exact same byte stream with zero recursion depth.

Upstream's recursion is fine in practice (FAT16 reads tend to be
small) but the property is not worth losing just to track the
diff. We keep the L64 loop. The L83 effect (adding the new
constructor) is the part that matters for the rest of module 1.

## What this unlocks

`fat16_resolve` and `fat16_init_private` were the last callers
that assumed FAT16 disks were id-addressable. With this change,
any FAT16 partition surfaced by GPT can be mounted. L84's FAT16
update (`volume_name` plumbing through `disk_create_new`) drops
the L78 `#if 0` guard and uses these streamers in anger.

## BIOS test status

Source-only as usual since L74. The build links; the test
verifies the new prototype is in the header, the definition is in
the .c, FAT16 init no longer calls the id-based constructor, and
the from-disk variant replaces it.
