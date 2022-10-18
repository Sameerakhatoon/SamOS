# Ch 83 - VFS fclose + FAT16 fclose

Book Ch 80 (VFS) and Ch 81 (FAT16) combined. Without `fclose` every
open file leaks both a `file_descriptor` and the FAT16 chain behind
it; with it the kernel can finally finish what it started.

## What got added

### Ch 80 - VFS (`src/fs/file.{h,c}`)

- `typedef int (*FS_CLOSE_FUNCTION)(void* private);`.
- `struct filesystem` gains `.close` between `.stat` and `name`.
- `int fclose(int fd);` prototype.
- Static helper `file_free_descriptor(desc)`: zero out the
  `file_descriptors[index-1]` slot and `kfree` the descriptor.
- `fclose(fd)`:
  - `file_get_descriptor(fd)`, return `-EIO` on null.
  - Dispatch to `desc->filesystem->close(desc->private)`.
  - If the driver returned `SAMOS_ALL_OK`, call
    `file_free_descriptor(desc)` so the descriptor table no longer
    points at freed memory.

### Ch 81 - FAT16 (`src/fs/fat/fat16.c`)

- Forward decl `int fat16_close(void* private);`.
- `struct filesystem fat16_fs` gets `.close = fat16_close`.
- `fat16_free_file_descriptor(desc)` (static): chain through
  `fat16_fat_item_free` (which already knows how to free a fat_item
  whether it wraps a file or a directory) and then `kfree(desc)`.
- `fat16_close(private)` is a one-line wrapper that just calls the
  helper and returns 0. Mirrors the book's structure.

## Smoke probe

```c
fclose(fd);
print(" afterclose=ok");
```

If `fat16_close` corrupts the heap or double-frees, the next `print`
either crashes or scrolls past visible VGA. Seeing `afterclose=ok` on
screen proves the close path returned cleanly.

`tests/34-fclose.sh` greps for `afterclose=ok`.

## Heap accounting

`fclose` releases four blocks at once:

1. The cloned `fat_directory_item` (from Ch 72's
   `fat16_clone_directory_item`).
2. The wrapping `fat_item`.
3. The `fat_file_descriptor`.
4. The outer VFS `file_descriptor`.

The free path also nulls out `file_descriptors[fd-1]` so the slot can
be reused by a future `fopen`. After fclose, the heap's first-free
slot is 1041 (instead of 1045 in Ch 75), so `km1` lands at
`0x01000000 + 1041 * 0x1000 = 0x01411000`. `tests/13-kmalloc-works.sh`
updated to match.

## Known book bug still standing

`fat16_open` still leaks the descriptor it kzalloc'd if
`fat16_get_directory_entry` returns null. The fix (one `kfree` on
the EIO path before returning) belongs in its own gotcha commit so
the book chapters stay traceable to git history.
