# Ch 62 - String utilities

Book Ch 58: stand up `src/string/string.{h,c}` with the four string
helpers FAT16 + path parsing will need: `strlen`, `strnlen`,
`isdigit`, `tonumericdigit`. Nothing about strings yet that the C
standard library wouldn't give us if we had one - we don't, so we own
this code.

## What got added

- `src/string/string.h` - prototypes for the four functions. Pulls in
  `<stdbool.h>` for `isdigit`'s return type.
- `src/string/string.c` - book-verbatim implementations:
  - `strlen(ptr)` walks until the null terminator.
  - `strnlen(ptr, max)` walks up to `max` chars, stops early on null.
    Used by the path parser to keep length checks bounded.
  - `isdigit(c)` true when c is in `'0'..'9'` (ASCII 48..57).
  - `tonumericdigit(c)` returns `c - '0'`, so `'7' -> 7`.
- `Makefile` - new object `./build/string/string.o` in `FILES` plus its
  build rule. The compile uses the same `-std=gnu99` + `$(INCLUDES)`
  flags as every other unit, with an extra `-I./src/string` so the C
  file finds its own header by `#include "string.h"`.
- `build.sh` - added `build/string` to the `mkdir -p` line.
- `src/kernel.c`
  - Removed the local `size_t strlen(const char* str)` that used to
    live here; it's now `int strlen(const char* ptr)` in
    `string/string.c`. The book deliberately uses `int` to match its
    own convention, not `size_t`.
  - `print()` now stores its length in `int` to match the new
    signature.
  - Added `#include "string/string.h"` near the top.
  - Added a smoke probe under the bootsig print:
    `print(" slen="); print_hex32(strnlen("hello", 100));`. The
    expected on-screen text is `slen=00000005`.

## Why one extra smoke probe

`print` already depends on `strlen` to know how many characters to copy
to the VGA buffer, so a broken `strlen` would knock out every test that
checks VGA contents. But broken `strnlen` / `isdigit` /
`tonumericdigit` wouldn't surface for another commit or two (they're
called only from the path parser arriving in Ch 59). Adding one
deliberate `strnlen` call now means `tests/17-string-utils.sh` catches
any breakage as soon as it happens, instead of waiting for downstream
features to fail mysteriously.

## Heap accounting hasn't changed

The smoke probe uses a string literal in `.rodata` and prints a single
hex value. No new heap allocations, so `tests/13-kmalloc-works.sh`
expected addresses (`0x01402000`, etc.) still hold.
