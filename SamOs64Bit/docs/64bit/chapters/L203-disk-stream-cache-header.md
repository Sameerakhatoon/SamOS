# Lecture 203 - disk stream cache header

Upstream: PeachOS64BitCourse f509231.

## What landed

`streamer.h` gains the cache type tower:

- `struct disk_stream_cache_sector`: a single 2 KB cache slot.
- `struct disk_stream_cache_bucket_level3`: 64 sectors + a
  round-robin counter.
- `struct disk_stream_cache_bucket_level2`: 1024 level-3
  buckets (a 32 MB region per bucket).
- `struct disk_stream_cache_bucket_level1`: 1024 level-2
  buckets (a 32 GB region per bucket).
- `struct disk_stream_cache_round_robin`: 1024-deep eviction
  queue.
- `struct disk_stream_cache`: dynamic level-1 array + round-
  robin queue.

Sizing macros walk the byte-count from the inside out.

`disk.h`:

- Forward declaration of `struct disk_stream_cache`.
- `struct disk` gains a `cache` pointer.

L204-L207 land the source.

## BIOS test status

Source + link. Build links.
