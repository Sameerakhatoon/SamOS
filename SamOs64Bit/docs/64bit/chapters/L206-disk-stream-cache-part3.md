# Lecture 206 - disk stream cache source part 3

Upstream: PeachOS64BitCourse 07638c1.

(Upstream commit message also reads "part three" - second one
in a row; preserved verbatim.)

## What landed

`disk.c`:

- `disk_real_sector`: absolute LBA from disk's starting_lba +
  caller's relative LBA.
- `disk_real_offset`: that LBA times the disk's sector_size.
  Used as the cache key.
- `disk_create_new` now allocates `disk->cache` via
  `diskstreamer_cache_new`.
- `#include "disk/streamer.h"` so the cache allocator is in
  scope.

`disk.h`: decls for the new helpers.

`streamer.h`: `diskstreamer_cache_new` exposed.

`streamer.c`:

- Both stream constructors now stash `disk->sector_size` on
  the stream.
- The L204 cache helpers' upstream typo
  (`disk_Stream_cache_bucket_level1`) is fixed to
  `disk_stream_cache_bucket_level1`.
- `diskstreamer_read` is replaced with a sector-aligned loop
  that consults the per-disk cache: hit reads from the cache
  buffer; miss goes to disk_read_block into the cache slot,
  then to the caller's buffer. A negative LBA span panics
  `"you went below zero\n"`.

The upstream missing-semicolon (L205's `res = cache_res`)
lands here as a fix.

## SamOs deviation

The L205 read body was held back; SamOs lands it here together
with `disk_real_offset` so everything compiles in one go.

## BIOS test status

Source + link. Build links.
