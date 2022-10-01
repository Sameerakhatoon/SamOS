# Ch 69 - FAT16 driver foundations

Book Ch 65: stand up the FAT16 driver shell. No actual FAT reading
yet; just the `struct filesystem`, the `init` entry, and stubs for
`resolve` / `open` so the VFS layer can dispatch into it.

## What got added

- `src/fs/fat/fat16.h` - one prototype: `struct filesystem* fat16_init();`.
- `src/fs/fat/fat16.c`:
  - File-static forward declarations for `fat16_resolve` and
    `fat16_open` (project style).
  - `struct filesystem fat16_fs = { .resolve = fat16_resolve, .open = fat16_open };`
  - `fat16_init` writes "FAT16" into `fat16_fs.name` via `strcpy` and
    returns the address. We keep the struct file-scope so its address
    is stable for the whole kernel lifetime.
  - `fat16_resolve(disk)` returns `0` (always claims the disk).
    Eventually this will read the BPB and verify the volume actually
    *is* FAT16; for now any disk works.
  - `fat16_open(disk, path, mode)` returns null.
- `src/fs/file.c` - `fs_static_load()` is no longer empty:
  ```c
  fs_insert_filesystem(fat16_init());
  ```
  So the FAT16 entry lands in `filesystems[0]` during `fs_init`.
- `Makefile` - new `./build/fs/fat/fat16.o` in `FILES` + build rule.
- `build.sh` - added `build/fs/fat` to `mkdir -p`.
- `src/kernel.c` - one more smoke probe under the fopen line:
  ```c
  print(" fs=");
  print(disk_get(0)->filesystem ? disk_get(0)->filesystem->name : "NONE");
  ```
  Output: `fs=FAT16`.

## End-to-end path the test exercises

1. `kernel_main` calls `fs_init()`.
2. `fs_init` zeroes `file_descriptors`, then calls `fs_load`.
3. `fs_load` zeroes `filesystems`, then calls `fs_static_load`.
4. `fs_static_load` calls `fat16_init()` and hands the result to
   `fs_insert_filesystem`, which writes it into `filesystems[0]`.
5. `kernel_main` calls `disk_search_and_init()`.
6. `disk_search_and_init` fills `disk.type`/`sector_size`, then calls
   `fs_resolve(&disk)`.
7. `fs_resolve` walks `filesystems[]`, sees slot 0 is non-null, calls
   `filesystems[0]->resolve(&disk)` which is `fat16_resolve` returning
   `0` (success), and returns `&fat16_fs`.
8. `disk.filesystem = &fat16_fs`.
9. Later, the smoke probe prints `disk_get(0)->filesystem->name`,
   which is the literal "FAT16" `strcpy`'d into the struct in step 4.

If any link in that chain breaks, `fs=` either prints `NONE` (resolve
came back null) or garbage (struct never got the name written).

## Why `fopen` test is still relevant

`fopen` still returns `-EIO` because we haven't wired the dispatch yet
(that's the next chapter). So `tests/21-vfs-fopen-stub.sh` should
keep passing alongside `tests/23-fat16-registers.sh` - the former
asserts "no dispatch yet", the latter asserts "filesystem registered
and resolved". When `fopen` learns to dispatch through `fat16_open`,
test 21's expected value will change.
