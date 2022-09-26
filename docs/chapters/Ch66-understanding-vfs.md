# Ch 66 - Understanding virtual filesystems (prose)

Book Ch 62: what a VFS is and why we need one. No source-tree changes.
Background reading before the next few chapters wire one into the
kernel.

## The shape of a VFS

A VFS is one indirection between "the kernel wants to open a file" and
"some filesystem driver reads sectors off a disk". Every filesystem
type (FAT16, ext2, NTFS, a tmpfs, a procfs) registers a table of
function pointers with the VFS. The VFS doesn't know how any of them
work; it just knows which one to call.

When the kernel handles `fopen("0:/foo.txt", "r")`:

1. VFS parses the path to extract the drive (we already have this -
   Ch 59 path parser splits `"0:/foo.txt"` into `drive_no=0` +
   `["foo.txt"]`).
2. VFS asks "which filesystem is mounted at drive 0?" via something
   like `fs_resolve(disk)`.
3. VFS calls the resolved filesystem's `open` function pointer with
   the parsed path.
4. The filesystem-specific open returns a handle the VFS records and
   hands back as a file descriptor.

`fread` / `fwrite` / `fclose` follow the same pattern: VFS looks up
the descriptor, finds the filesystem it belongs to, dispatches to that
filesystem's function pointer.

## What lands in our kernel

From Ch 63 onwards we'll add three layers in order:

1. **VFS dispatch layer** (`src/fs/file.{h,c}` in the book): the
   function-pointer table types (`struct filesystem`), the global
   registration list, `fs_resolve`, and the public `fopen`/`fread`/etc.
   shells that route to the resolved filesystem.
2. **Filesystem registration**: each driver (just FAT16 to start)
   provides a `struct filesystem` and calls `fs_insert_filesystem`.
3. **FAT16 driver**: implements the function-pointer entries by
   walking the BPB + FAT + root directory + clusters laid out in
   Ch 65's boot sector.

## Why bother for a hobby kernel

We only have one disk and one filesystem; we could hard-wire FAT16
into the kernel and skip the dispatch table. We don't, because:

- Two-line addition for a second filesystem later (RAM disk, ELF
  bundles, virtual procfs-style endpoints) instead of a global rewrite.
- The dispatch layer is where path parsing, descriptor lifetime, and
  error mapping all live. Cleaner to design it once.
- Mirrors how real kernels (Linux, BSD) are built, which makes the
  book's later chapters (ELF, syscalls, shell) translate without
  surgery.

## Reference shapes

The book sketches function pointers roughly like:

```
typedef void* (*FS_OPEN_FUNCTION)(struct disk*, struct path_part*, int mode);
typedef int   (*FS_READ_FUNCTION)(struct disk*, void* private, uint32_t size, uint32_t nmemb, char* out);
typedef int   (*FS_RESOLVE_FUNCTION)(struct disk*);

struct filesystem {
    FS_RESOLVE_FUNCTION resolve;
    FS_OPEN_FUNCTION    open;
    FS_READ_FUNCTION    read;
    char                name[20];
};
```

The actual signatures we'll commit are whatever the next chapter
dictates; this is just the mental model.
