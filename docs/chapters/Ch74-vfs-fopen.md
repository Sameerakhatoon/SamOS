# Ch 74 - VFS fopen

Book Ch 70: replace the `-EIO` stub `fopen` with the real dispatch
through the filesystem table.

## What got added

- `src/kernel.h` - three macros for cross-layer error handling:
  - `ERROR(value)` casts to `void*` (filesystem driver hooks return
    `void*`, so error codes need to round-trip through that type).
  - `ERROR_I(value)` casts to `int` (read the int back out of a
    pointer-shaped error).
  - `ISERR(value)` true when `(int)value < 0`.
- `src/fs/file.h` - `fopen`'s second arg renamed from `mode` to
  `mode_str` so the implementation can keep a local `FILE_MODE mode`.
  Purely cosmetic.
- `src/fs/file.c`:
  - Includes `string/string.h` and `disk/disk.h`.
  - `file_get_mode_by_string(str)` (file-static) - reads the first
    byte of `"r"`/`"w"`/`"a"` and returns the matching
    `FILE_MODE_*` constant, or `FILE_MODE_INVALID` on anything else.
  - Real `fopen(filename, mode_str)`:
    1. `pathparser_parse(filename, NULL)`. Reject (`-EINVARG`) on
       null result or on a path with no segments (a bare drive).
    2. `disk_get(root_path->drive_no)`. Reject (`-EIO`) on null disk
       or no mounted filesystem.
    3. Map mode string to enum; reject (`-EINVARG`) on
       `FILE_MODE_INVALID`.
    4. Dispatch to `disk->filesystem->open(disk, root_path->first, mode)`.
       Use `ISERR` to convert a pointer-shaped error back to an int.
    5. Allocate a new file descriptor via `file_new_descriptor`,
       fill in `filesystem`/`private`/`disk`, return the descriptor's
       1-based `index`.
    6. Coerce any negative `res` back to `0` before returning,
       because `fopen` is contractually non-negative.
- `src/kernel.c` - three new probes covering the dispatch:
  - `fop_ok=` prints `fopen("0:/anything", "r")` -> expect `1`.
  - `fop_bad=` prints `fopen("notapath", "r")` -> expect `0` (path
    parser rejects).
  - `fop_mod=` prints `fopen("0:/anything", "z")` -> expect `0`
    (mode string rejected).

## A book-shipped wart

`fat16_open` is still the Ch 65 stub returning `0` (null). Null is
*not* an error per `ISERR`, so `fop_ok` returns a freshly allocated
descriptor for a path that doesn't actually exist on the volume. That
matches the book's mid-arc state; Ch 71+ will plug in a real
`fat16_open` that surfaces "file not found" as `-EIO` or similar, at
which point the `fop_ok` probe will need updating.

## Heap accounting

Each well-formed `fopen` call leaks heap intentionally - the parsed
`path_root` and its `path_part` linked list aren't `pathparser_free`'d,
and the descriptor allocation persists. Three smoke calls add up to:

- fopen #1 valid: path_root + segment string + path_part struct +
  trailing get_path_part (transient, freed) + descriptor. The
  freed slot is reused by the descriptor.
- fopen #2 malformed: path parser rejects before any alloc.
- fopen #3 bad mode: path_root + segment string + path_part +
  trailing get_path_part (freed); no descriptor.

So 7 persistent blocks plus a freed-then-not-reused slot at the top.
The kmalloc smoke probe lands at slot 1038 -> `km1=0x0140E000`.
`tests/13` updated with the math.

## Test 21 retired

`tests/21-vfs-fopen-stub.sh` was asserting the now-obsolete
`-EIO`/`0xFFFFFFFF` stub return. Deleted in this commit; the new
behaviour is covered by `tests/28-vfs-fopen-dispatch.sh`. The Ch 63
commit still ships test 21 in git history as the chapter-time snapshot.
