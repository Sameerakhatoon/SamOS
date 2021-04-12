# G03 - Stale .o files on header edits

## Symptom

Test 23 (FAT16 registers) flipped from `fs=FAT16` to `fs=6` after the
Ch 73 commit. The kernel was correctly resolving FAT16 to the
filesystem table; the printed name field just had garbage.

## Root cause

The Ch 73 commit added a `FS_READ_FUNCTION read;` member to
`struct filesystem` in `src/fs/file.h`, sliding the `name[20]` field
4 bytes later in the struct.

Our Makefile compiles each `.c` with its include directory but has
**no header dependency rules**. `fat16.o` was up to date by mtime
(its `.c` hadn't changed), so make didn't rebuild it. That meant:

- `fat16.o` was still using the *old* `struct filesystem` layout
  (name at offset 8).
- `kernel.o` and `file.o` were rebuilt with the *new* layout
  (name at offset 12).

When `fat16_init` ran its `strcpy(fat16_fs.name, "FAT16")`, it wrote
to the old offset; when `kernel_main` printed `disk->filesystem->name`,
it read from the new offset. The character we saw (`'6'`) was the
last byte of "FAT16" landing 4 bytes past where the new code looked.

## Fix (this commit)

`build.sh` now runs `make clean` before each build, forcing a full
recompile every time. The Makefile is small enough that the wasted
work is irrelevant and the silent-corruption risk on header edits
disappears.

## Future-proof option

If the build ever gets large enough that the clean-rebuild cost
matters, the right move is to add `-MMD -MP` to the compile flags
and `-include $(FILES:.o=.d)` to the Makefile. That generates per-
object `.d` files containing the actual header dependencies and lets
make rebuild only what's truly affected.
