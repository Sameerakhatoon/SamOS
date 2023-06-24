# Lecture 64 - filesystem + disk bug fixes

**Source commit (PeachOS64BitCourse):** `2655b95`
**SamOs commit:** L64 on `module1-64bit` branch
**Regression test:** `tests64/L64-fs-disk-fixes.sh`

## Why this chapter exists

The original FAT16 / disk_streamer code had three latent bugs
that bit when reading larger files (e.g. multi-MiB ELFs):

1. `diskstreamer_read` was recursive across sector boundaries.
   One C frame per sector -> a 4 KB read used 8 frames, a
   2 MiB read used 4096. Stack overflow on real files.
2. `fat16_get_cluster_for_offset`'s end-of-chain check tested
   `entry == 0xFF8 || entry == 0xFFF` (12-bit-FAT values). The
   actual FAT16 end-of-chain range is `0xFFF8..0xFFFF` (any
   value with the high 13 bits = 0x1FFF). A correctly-terminated
   chain with end value 0xFFFE would have been mis-treated as
   a normal cluster id and walked into garbage.
3. `fat16_read_internal_from_stream` was recursive in the same
   way - one frame per cluster.

All three become iterative. `fat16_get_cluster_for_offset`'s
end-of-chain now returns `-EOUTOFRANGE` (new error code in
status.h) so the caller can distinguish "honest EOF" from
"disk error". The function signature widens to `uint16_t`
matching the FAT entry size.

## Iterative diskstreamer_read

```c
while (remaining > 0) {
    sector = pos / SECTOR_SIZE;
    offset = pos % SECTOR_SIZE;
    chunk  = min(SECTOR_SIZE - offset, remaining);
    disk_read_block(...);
    memcpy(out, buf + offset, chunk);
    out       += chunk;
    pos       += chunk;
    remaining -= chunk;
}
```

The clamp `min(SECTOR_SIZE - offset, remaining)` keeps chunks
inside the current sector. After each iteration `pos` is at a
sector boundary, so `offset = 0` and chunk is full-sector unless
the last bit is short.

## Iterative fat16_read_internal_from_stream

Same shape: walk cluster by cluster, accumulate `bytes_read`,
return total at the end. Calling `fat16_get_cluster_for_offset`
EVERY iteration looks wasteful but the alternative is
maintaining the "current cluster" state, which the old recursive
form did by re-passing `cluster + offset+total_to_read` to
itself. The new code is simpler and the FAT walk cost is
negligible compared to the per-sector disk read.

## How we verified

VGA after L64:

```
Hello 64-bit!
...
tss ready
Loading program...
user enter
```

Same tokens as L63. SIMPLE.BIN (2 bytes) doesn't exercise the
multi-cluster / multi-sector paths. The fix matters once we
load real-size files (L66 will when the ELF64 loader lands).

## Forward look

L65 - ELF loader refactored to ELF64: struct elf64_phdr,
elf64_shdr, e_entry / e_shoff / e_phoff widened.
L66 - first ELF program (blank.elf) actually runs - the L64
multi-cluster fix becomes load-bearing here.
