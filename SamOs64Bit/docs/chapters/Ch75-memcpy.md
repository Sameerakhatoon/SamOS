# Ch 75 - memcpy

Book Ch 71: one-function chapter. `memcpy(dest, src, len)` lands in
`memory.{h,c}` because the FAT16 driver's next chapter needs to copy
parsed directory entries and filename bytes.

## What got added

- `src/memory/memory.h` - prototype
  `void* memcpy(void* dest, void* src, int len);`.
- `src/memory/memory.c` - byte-at-a-time copy, returns `dest` (libc
  convention).
- `src/kernel.c` - includes `memory/memory.h` (was missing - other
  callers reached `memcpy` indirectly via `string.h` once it lands in
  the FAT16 driver, but the kernel-side smoke probe uses it
  directly). Smoke probe under the Ch 69 line:
  ```c
  char mbuf[8] = {0};
  memcpy(mbuf, "abc", 3);
  print(" mcp="); print_hex32(strlen(mbuf));
  ```
  Expected `mcp=00000003`.

## Why not overlap-safe

`memcpy` here doesn't handle overlapping ranges; libc reserves that
for `memmove`. Nothing in the FAT16 driver overlaps its source and
destination, so we don't add `memmove` until something needs it.
