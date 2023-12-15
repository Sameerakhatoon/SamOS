# Lecture 204 - disk stream cache source part 1

Upstream: PeachOS64BitCourse 9f700fd.

## What landed

- `diskstreamer_cache_new`: kzalloc the cache record.
- `diskstreamer_cache_bucket_level1_get`: lazily grow the
  level-1 array to `index + 1` via `krealloc`, zero the new
  tail, allocate the bucket entry on first access.
- `diskstreamer_cache_bucket_level2_get` /
  `diskstreamer_cache_bucket_level3_get`: lazily allocate the
  child bucket inside the parent's fixed-size array.
- `diskstreamer_cache_bucket_level3_free`: kfree each cache
  sector and then the bucket itself.

## Upstream typo preserved

Inside the level-1 grow path the sizeof uses the typo'd struct
name `disk_Stream_cache_bucket_level1` (note the capital `S`).
gcc accepts it as a forward decl of a different struct; sizeof
of a pointer-to-pointer-to-struct is `sizeof(void*)`, so the
byte-count math coincidentally lines up with the real type.
Preserved verbatim per the project rule.

L205-L207 wire the lookup + read flow.

## BIOS test status

Source + link. Build links.
