# Ch 82 - VFS fstat + FAT16 fstat

Book Ch 78 (VFS) and Ch 79 (FAT16 implementation) bundled into one
commit because they're a tight pair: the VFS plumbing for fstat is
only useful once a driver behind it actually fills the
`struct file_stat`.

## What got added

### Ch 78 - VFS layer (`src/fs/file.{h,c}`)

- `enum { FILE_STAT_READ_ONLY = 0b00000001 };` plus
  `typedef unsigned int FILE_STAT_FLAGS;`. Bit field today; more
  bits land when we model permissions in user space.
- `struct file_stat { FILE_STAT_FLAGS flags; uint32_t filesize; };`.
- `typedef int (*FS_STAT_FUNCTION)(struct disk*, void* private,
  struct file_stat*);`.
- `struct filesystem` gains `.stat` between `seek` and `name`.
- `int fstat(int fd, struct file_stat* stat);` prototype and impl:
  `file_get_descriptor(fd)`, dispatch to
  `desc->filesystem->stat(desc->disk, desc->private, stat)`. Same
  shape as `fread`/`fseek`.

### Ch 79 - FAT16 layer (`src/fs/fat/fat16.c`)

- Forward decl `int fat16_stat(struct disk*, void* private,
  struct file_stat* stat);`.
- `struct filesystem fat16_fs` gets `.stat = fat16_stat`.
- Real implementation:
  - Cast `private` to `fat_file_descriptor*`, pull out the wrapped
    `fat_item`.
  - Reject non-`FAT_ITEM_TYPE_FILE` with `-EINVARG` (the
    fat_item->item field would point at the wrong struct otherwise).
  - Copy `ritem->filesize` into `stat->filesize`.
  - Map `ritem->attribute & FAT_FILE_READ_ONLY` into
    `stat->flags |= FILE_STAT_READ_ONLY`.

## Smoke probe

`kernel_main` (after the existing fseek+fread probe) now:

```c
struct file_stat s = { 0 };
fstat(fd, &s);
print(" fz=");  print_hex32(s.filesize);
print(" ffl="); print_hex32(s.flags);
```

hello.txt is 13 bytes of `"Hello world!\n"` with no read-only flag,
so the VGA buffer shows `fz=0000000D ffl=00000000`.
`tests/33-fstat.sh` greps for both.

## Why the union access still works after Ch 72's bug

Recall `fat16_new_fat_item_for_directory_item` unconditionally sets
`type = FAT_ITEM_TYPE_FILE`. That's a bug for actual subdirectories
but a happy accident for our test: the fat_item we hand to
`fat16_stat` is tagged FILE, so the union access `desc_item->item`
(which expects a `fat_directory_item*`) reads from the correct
field. When we eventually fix the type tagging, fstat will start
correctly rejecting directory descriptors, which is the right
behavior.

## Heap

No new allocations on the open path. The smoke probe `struct
file_stat` lives on the stack, the dispatch reads from the existing
fat_file_descriptor and cached fat_directory_item, then returns.
`tests/13-kmalloc-works.sh` block counts unchanged.
