# Ch 68 - strcpy

Book Ch 64: a one-function chapter. We need `strcpy` because the FAT16
driver lands in the next commit and its `fat16_init` does
`strcpy(fat16_fs.name, "FAT16");`.

## What got added

- `src/string/string.h` - prototype `char* strcpy(char* dest, const char* src);`
- `src/string/string.c` - copy each byte from `src` to `dest` until the
  null terminator, then write the terminator to `dest`. Return the
  original `dest` (libc convention) so callers can chain.
- `src/kernel.c` smoke probe under the strnlen probe:
  ```c
  char sbuf[8];
  strcpy(sbuf, "hi");
  print(" scp=");
  print(sbuf);
  print_hex32(strlen(sbuf));
  ```
  On-screen result: `scp=hi00000002`.

## Edge cases this doesn't cover

`strcpy` here has no length cap; the FAT16 driver passes a 5-byte
literal into a 20-byte field, so a buffer overflow can't happen for
its intended use. A future commit may introduce `strncpy` when we
copy untrusted (path) strings.
