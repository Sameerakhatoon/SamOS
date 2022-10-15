# Ch 81 - FAT16 fseek

Book Ch 77: plug a real `seek` into the FAT16 driver and prove it
works by reading a file mid-way through.

## What got added

- `src/status.h` - `EUNIMP = 7`. Returned by `fat16_seek` when asked
  for `SEEK_END` (book stops short of implementing it).
- `src/fs/fat/fat16.c`:
  - Forward decl `int fat16_seek(void* private, uint32_t offset,
    FILE_SEEK_MODE seek_mode);`.
  - Real implementation:
    - Cast `private` back to `struct fat_file_descriptor*`.
    - Reject if the wrapped `fat_item` isn't a `FAT_ITEM_TYPE_FILE`
      (no directory-seeks).
    - Reject if `offset >= ritem->filesize` (book's bounds check;
      no negative offsets in the unsigned API).
    - `SEEK_SET` -> `desc->pos = offset`.
    - `SEEK_CUR` -> `desc->pos += offset`.
    - `SEEK_END` -> `-EUNIMP` (deferred).
  - `struct filesystem fat16_fs` now also sets `.seek = fat16_seek`,
    so the VFS `fseek` dispatch lands somewhere real.
- `src/kernel.c` - the Ch 75 smoke probe is upgraded:
  ```c
  int fd = fopen("0:/hello.txt", "r");
  if(fd){
      char hbuf[14];
      fseek(fd, 2, SEEK_SET);
      fread(hbuf, 11, 1, fd);
      hbuf[11] = 0;
      print("\nhs="); print(hbuf);
  }
  ```
  hello.txt is `"Hello world!\n"` so after `fseek(2, SET)` the read
  yields `"llo world!\n"`. On-screen: `hs=llo world!`.
- `tests/32-fat16-read.sh` updated to grep for `hs=llo world!`
  instead of the Ch 75 `h=Hello world!`. Same test file because the
  scenario is the same kind of end-to-end check, just shifted by the
  seek.

## Why no separate test

The Ch 75 test already proved fopen + fread; this commit adds
fseek to the same flow. Re-running the test against the seeked
string proves both the fseek dispatch from the VFS layer AND the
FAT16 driver's SEEK_SET branch in one shot. Adding a second test
that does the unseeked read again wouldn't catch anything new.

## Deferred

- `SEEK_END` returns `EUNIMP`. We'll need it for shells / `cat`
  later, but the book doesn't ask for it now.
- `fat16_seek` doesn't kfree anything; the `seek` lives entirely
  inside the existing descriptor. The known book bug from Ch 72
  (`fat16_open` leaks descriptor on `-EIO`) is still on the gotcha
  punch-list.
