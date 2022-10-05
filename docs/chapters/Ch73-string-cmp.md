# Ch 73 - String comparison utilities

Book Ch 69: a four-helper chapter to round out `string.{h,c}` ahead of
the VFS `fopen` implementation and the FAT16 directory-walking code.

## What got added (all in src/string/string.c)

- `tolower(c)` - if `c` is ASCII 65..90 ('A'..'Z'), add 32, returning
  the lowercase letter. Otherwise return `c` unchanged. Used as a
  primitive by `istrncmp`.
- `strncmp(str1, str2, n)` - libc-style: compare up to `n` bytes
  byte-by-byte. Returns the signed difference of the first mismatching
  byte, 0 on equal-then-null-terminator or `n` reached. Used by the
  VFS to compare modes ("r" / "rw" / etc.) and by the FAT16 driver to
  compare filenames as parsed off disk.
- `istrncmp(s1, s2, n)` - case-insensitive sibling: same loop, but a
  mismatch only counts if `tolower(u1) != tolower(u2)`. FAT16 stores
  filenames in 8.3 uppercase; user paths come in mixed case, so the
  driver needs the case-insensitive variant for the lookup.
- `strnlen_terminator(str, max, terminator)` - libc has no equivalent;
  counts bytes until any of: null, `terminator`, or `max` is reached.
  The FAT16 driver uses this with terminator=`' '` to strip the
  space-padded 8.3 filename to a real length.

All four prototypes added to `src/string/string.h`.

## Three smoke probes

`kernel_main` prints three values next to the Ch 64 line:

- `istr=00000001` - `istrncmp("AbC","abc",3) == 0`.
- `scmp=00000001` - `strncmp("abc","abd",3) != 0`.
- `sterm=00000003` - `strnlen_terminator("foo bar",100,' ') == 3`.

If any of the four helpers is wrong, at least one probe shifts to 0
(or a non-3 hex) and `tests/27-string-cmp.sh` fails.

## Why we need these *now*

Ch 70 (next commit) wires `fopen` through the VFS dispatch table.
That code parses the mode string ("r" / "w" / "a"), which `strncmp`
handles. Ch 71+ implements FAT16's `fat16_open`, which compares
case-insensitive 8.3 names against parsed path parts. Without these
helpers neither chapter can land.
