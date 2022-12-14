# Ch 119 - Stream reader bug fix (cross-sector reads)

The book retroactively fixes a bug in `diskstreamer_read` from Ch 60. The old code clamped `total_to_read = min(total, SECTOR_SIZE)`, ignoring the existing `offset` within the sector. When the read started partway into a sector AND wanted to read past the sector boundary, the inner loop would walk off the end of `buf[]` and the recursive call would re-read the wrong sector.

## Old code (Ch 60)

```c
int total_to_read = total > SAMOS_SECTOR_SIZE ? SAMOS_SECTOR_SIZE : total;
...
if (total > SAMOS_SECTOR_SIZE) {
    res = diskstreamer_read(stream, out, total - SAMOS_SECTOR_SIZE);
}
```

A read of 8 bytes at `pos = 0x1FC` (offset 0x1FC within sector 0, only 4 bytes left in this sector) would compute `total_to_read = 8`, then read `buf[0x1FC..0x203]` - 4 of those bytes are off the end of `buf`. The recursive tail subtracted `SECTOR_SIZE` from total even though we only consumed 4 bytes.

## New code

Compute `overflow` from `(offset + total_to_read) >= SECTOR_SIZE`. If overflow, shrink `total_to_read` so it stops exactly at the sector boundary. The recursive tail subtracts the *actual* amount consumed (`total - total_to_read`) rather than a hard-coded sector.

```c
int  total_to_read = total;
bool overflow = (offset + total_to_read) >= SAMOS_SECTOR_SIZE;
if (overflow) {
    total_to_read -= (offset + total_to_read) - SAMOS_SECTOR_SIZE;
}
...
if (overflow) {
    res = diskstreamer_read(stream, out, total - total_to_read);
}
```

## Test impact

- The bug was latent for us - nothing in the current suite makes a cross-sector seek+read - so no functional test changes are required for the streamer itself. The fix is shipped purely to keep parity with the book before the ELF loader starts hammering the streamer in Ch 121+.
- Several tests had their EIP-range upper bounds widened because Ch 112's macro-generated interrupt table inflated the kernel binary past their old bounds (`tests/08-kernel-loaded.sh`, `tests/09-kernel-main-runs.sh`). Drift, not regression.
- `tests/38-syscall-roundtrip.sh` was relaxed to accept either a ring-3 EIP in the user binary or a ring-0 EIP in the kernel image. Post-Ch 117 the user runs a tight getkey/putchar loop that funnels through int 0x80 every iteration, so the sampled register snapshot lands in kernel mode about half the time. Both "in user binary" and "in kernel servicing the syscall" prove the pipeline is alive.
