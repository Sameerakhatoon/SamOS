# Ch 63 - Path parser

Book Ch 59: build a parser that turns `"0:/bin/shell.exe"` into a
`path_root` whose linked list of `path_part`s gives the filesystem
layer something it can walk segment by segment.

## What got added

- `src/kernel.h` - new constant `SAMOS_MAX_PATH = 108`. Used as a cap
  for `strnlen` checks and as the buffer size for each parsed segment.
- `src/status.h` - new `EBADPATH = 4` so the parser can distinguish
  "this string isn't shaped like a path" from generic I/O failures.
- `src/memory/memory.{h,c}` - added `memcmp(s1, s2, count)`. Returns
  -1/0/1 like the libc version. The parser uses it to test for the
  `":/"` after the drive digit.
- `src/fs/pparser.h` - exposes two structs (`path_root` and
  `path_part`) plus `pathparser_parse` and `pathparser_free`. Everything
  else is file-static.
- `src/fs/pparser.c` - file-static helpers forward-declared at the top
  (project style):
  - `pathparser_path_valid_format` - true when the path has at least 3
    characters, starts with a digit, and bytes 1-2 are `":/"`.
  - `pathparser_get_drive_by_path` - validates, reads the digit via
    `tonumericdigit`, advances the cursor past `"N:/"`. Returns
    `-EBADPATH` on failure.
  - `pathparser_create_root` - kzalloc one `path_root`, fill in
    `drive_no`, leave `first = 0`.
  - `pathparser_get_path_part` - kzalloc a `SAMOS_MAX_PATH`-byte buffer
    and copy bytes until the next `/` or null. Skips the `/` so the
    next call starts at the next segment. Returns null and frees the
    buffer when the segment is empty (so the trailing `/` case doesn't
    produce a phantom empty part).
  - `pathparser_parse_path_part` - wraps `get_path_part`, allocates a
    `path_part` struct holding the segment string, and links it to the
    previous part if one was passed in.
  - `pathparser_free` - walks the list, frees each segment string and
    each `path_part`, then frees the `path_root`.
  - `pathparser_parse` (public) - bound-checks the whole path against
    `SAMOS_MAX_PATH`, peels the drive, allocates the root, parses the
    first segment, then loops `pathparser_parse_path_part` until it
    returns null.
- `Makefile` - new `./build/fs/pparser.o` in `FILES` plus its build
  rule.
- `build.sh` - added `build/fs` to `mkdir -p`.
- `src/kernel.c` - added `#include "fs/pparser.h"` and a smoke probe
  in `kernel_main` after `enable_interrupts()`:
  ```c
  struct path_root* root_path = pathparser_parse("0:/bin/shell.exe", NULL);
  print("\npp_drv="); print_hex32(root_path ? root_path->drive_no : 0xDEAD);
  if(root_path && root_path->first){
      print(" pp_p1="); print(root_path->first->part);
      if(root_path->first->next){
          print(" pp_p2="); print(root_path->first->next->part);
      }
  }
  ```
  The on-screen result is `pp_drv=00000000 pp_p1=bin pp_p2=shell.exe`.

## Heap usage knock-on

The parse for `"0:/bin/shell.exe"` consumes 6 heap blocks before the
kmalloc smoke probe runs: 1 path_root, 2 path_part structs, 2 segment
strings (108 bytes each, one block each), plus 1 trailing
`get_path_part` call that allocates a buffer, finds the input is
exhausted, then `kfree`s it - but because `kfree` reclaims its block
only into the next-alloc bookkeeping (it doesn't slide later allocs
back), the block still counts toward the pre-probe high-water mark.

So the kmalloc smoke probe addresses now start at
`0x01000000 + 1031 * 0x1000 = 0x01407000` (1025 paging blocks + 6 parser
blocks). `tests/13-kmalloc-works.sh` updated to expect that.

## How test 18 confirms the parse

`tests/18-path-parser.sh` greps the VGA buffer for three substrings:

- `pp_drv=00000000` proves the drive digit was extracted and
  converted to an int.
- `pp_p1=bin` proves the first segment was carved off, terminated,
  and stored in `first->part`.
- `pp_p2=shell.exe` proves the linked list moved past the first
  segment correctly and the trailing segment was also parsed.

If any of `pathparser_path_valid_format`, the drive offset advance, or
the segment loop is wrong, at least one of those three greps will
fail.
