# Ch 77 - VFS fread

Book Ch 73: stand up `fread` in the VFS layer. The dispatch into the
filesystem driver gets wired today; the FAT16 driver's `read`
implementation lands in Ch 75.

## What got added

- `src/fs/file.h`
  - `#include <stdint.h>` (we need `uint32_t` for the read function
    pointer's parameter types).
  - `typedef int (*FS_READ_FUNCTION)(struct disk* disk, void* private,
    uint32_t size, uint32_t nmemb, char* out);` - the type each
    filesystem driver fills in.
  - `struct filesystem` gets a new `FS_READ_FUNCTION read;` field
    between `open` and `name`. Designated-initialised drivers (FAT16
    only at the moment) get `.read = NULL` implicitly until their
    read function lands.
  - Public `fread` prototype:
    `int fread(void* ptr, uint32_t size, uint32_t nmemb, int fd);`.
- `src/fs/file.c` - real `fread` implementation:
  1. Reject `size == 0`, `nmemb == 0`, or `fd < 1` with `-EINVARG`.
  2. `file_get_descriptor(fd)` lookup; null -> `-EINVARG`.
  3. Dispatch to `desc->filesystem->read(desc->disk, desc->private,
     size, nmemb, ptr)`.

## Smoke probe

```c
char fbuf[4];
print(" frd="); print_hex32((unsigned)fread(fbuf, 1, 1, 0));
```

`fd=0` is always invalid (descriptors start at 1), so we hit the
EINVARG short-circuit. Expected on-screen `frd=FFFFFFFE` (the
unsigned cast of `-EINVARG = -2`). `tests/31-vfs-fread.sh` greps for
this.

## Why test with fd=0 instead of a real fopen handle

The 16 MiB FAT volume is empty (Ch 65/61 haven't mcopy'd any files
yet), so `fopen("0:/anything","r")` returns 0. We have no valid
descriptor to read from yet. Testing the dispatch with an invalid
fd at least confirms `fread` is callable from `kernel_main` and
returns the documented error code; the real read-through-FAT16 path
gets covered when we mcopy a file into the volume.

## G03 caught in this commit

The Ch 73 commit also adds `make clean` to `build.sh`. Why: changing
`struct filesystem`'s layout silently broke `fat16.o` (which wasn't
recompiled because its .c hadn't changed) while rebuilding `file.o`
and `kernel.o`. The two objects then disagreed on field offsets and
`fs=FAT16` started printing as `fs=6`. See
`docs/gotchas/G03-stale-objects-on-header-edit.md`.

## Ch 74 will be a docs-only note

Book Ch 74 corrects an OCR-introduced typo in Ch 72's
`fat16_read_internal_from_stream` (`* offset_from_cluster` should be
`+`). Our Ch 72 commit already shipped the `+` because the `*` would
have made every read crash. Ch 74 will be a no-op code-wise; just a
chapter note acknowledging the book correction.
