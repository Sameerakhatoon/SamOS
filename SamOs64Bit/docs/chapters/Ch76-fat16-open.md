# Ch 76 - FAT16 open

Book Ch 72: replace the `fat16_open` stub with the real one. The
driver now walks the parsed root directory, matches a filename, and
hands back a `fat_file_descriptor`. Read still isn't wired (next
chapter), but the path-to-disk dance is fully live.

## What got added

- `src/status.h` - `ERDONLY = 6`. Returned when `fat16_open` is asked
  for anything other than `FILE_MODE_READ`.
- `src/fs/fat/fat16.c`:
  - Includes `kernel.h` so the `ERROR`/`ISERR` macros are in scope.
  - Renamed `struct fat_item_descriptor` to `struct fat_file_descriptor`
    per the book.
  - Sixteen new file-static helpers (no header changes; everything
    funnels through the existing `fat16_open`):
    - `fat16_to_proper_string(out, in)` - strip the FAT16 trailing-
      space padding off a filename component.
    - `fat16_get_full_relative_filename(item, out, max_len)` - rebuild
      `"name.ext"` from the 8.3 padded fields, leaving the dot out if
      `ext[0]` is null or space.
    - `fat16_clone_directory_item(item, size)` - kzalloc + memcpy a
      `fat_directory_item` (used so the file descriptor owns its own
      copy of the directory entry, not a pointer into the cached
      table).
    - `fat16_get_first_cluster(item)` - splice the high-16 and low-16
      cluster fields back together.
    - `fat16_cluster_to_sector(private, cluster)` - convert a cluster
      id to its absolute sector (`ending_sector_pos + (cluster - 2) *
      sectors_per_cluster`; FAT16 reserves clusters 0 and 1).
    - `fat16_get_first_fat_sector(private)` - returns
      `reserved_sectors` from the BPB.
    - `fat16_get_fat_entry(disk, cluster)` - seek into FAT 1, read a
      16-bit entry, return it. Used to traverse cluster chains.
    - `fat16_get_cluster_for_offset(disk, start_cluster, offset)` -
      walk the FAT chain `offset / cluster_size` hops and return the
      cluster covering that byte offset; reject end-of-chain, bad-
      sector, and reserved markers with `-EIO`.
    - `fat16_read_internal_from_stream(disk, stream, cluster, offset,
      total, out)` - sector-stream the actual bytes, recursing into
      the next cluster when `total > cluster_size`.
    - `fat16_read_internal(disk, start_cluster, offset, total, out)` -
      wrapper that picks `cluster_read_stream`.
    - `fat16_free_directory(directory)` and `fat16_fat_item_free(item)` -
      mirror destructors.
    - `fat16_load_fat_directory(disk, item)` - for an item flagged as
      `FAT_FILE_SUBDIRECTORY`, allocate a `fat_directory`, read its
      cluster into memory, return it.
    - `fat16_new_fat_item_for_directory_item(disk, item)` - wrap a
      `fat_directory_item` in a `fat_item` (also handles subdirectory
      recursion). **Book bug preserved**: the function unconditionally
      sets `type = FAT_ITEM_TYPE_FILE` *after* the subdirectory
      branch, so any subdirectory item gets re-tagged as a file. Will
      need a g-fix once subdirectory traversal exercises this.
      Annotated in the code so the bug isn't accidentally "cleaned up"
      out of recognition.
    - `fat16_find_item_in_directory(disk, directory, name)` -
      linear scan, `istrncmp` against each entry's rebuilt full
      filename.
    - `fat16_get_directory_entry(disk, path)` - resolve the head of
      the path against the root directory, then walk
      `path->next->next...` against the subdirectory chain until the
      path is exhausted or a non-directory tag stops the walk.
  - Real `fat16_open`:
    1. Reject any non-READ mode with `ERROR(-ERDONLY)`.
    2. kzalloc a `fat_file_descriptor`. Out-of-memory -> `ERROR(-ENOMEM)`.
    3. Call `fat16_get_directory_entry`. Null result -> `ERROR(-EIO)`.
    4. Otherwise zero the position and return the descriptor.
- `src/kernel.c` - replaced the Ch 70 `fop_ok`/`fop_bad`/`fop_mod`
  probes with one focused probe:
  ```c
  print(" fop_miss=");
  print_hex32((unsigned int)fopen("0:/hello.txt", "r"));
  ```
  Expected `fop_miss=00000000` because the FAT volume is empty (no
  `mcopy` yet) so the root directory's first entry is `\0`-terminated
  and the lookup returns null -> `fopen` returns 0.

## Heap accounting

`fopen("0:/hello.txt", "r")` now allocates:

- 4 path blocks (path_root, segment string, path_part struct,
  trailing string that gets freed).
- 1 `fat_file_descriptor` (leaks - the book code doesn't kfree it on
  the `ERROR(-EIO)` early return; tagged for a future g-commit).

Net: 4 persistent + the freed trailing slot reused by the descriptor.
Block 1035 is the first free slot when the kmalloc smoke probe runs:
`0x01000000 + 1035 * 0x1000 = 0x0140B000`. `tests/13` updated.

## Test 28 retired

`tests/28-vfs-fopen-dispatch.sh` asserted the Ch 70 stub state where
`fat16_open` returned `null` -> a freshly allocated descriptor was
handed out for any path. Ch 72 replaces that with a real lookup, so
the assertion `fop_ok=00000001` no longer matches the codebase.
Deleted in this commit; the Ch 70 commit retains the test in git
history. `tests/30-fat16-open-miss.sh` is the new chapter-time check.

## Known book bugs preserved

- `fat16_new_fat_item_for_directory_item` always overwrites `type`
  with `FAT_ITEM_TYPE_FILE`, even when the item is a subdirectory.
- `fat16_open` returns `ERROR(-EIO)` without freeing the descriptor it
  kzalloc'd. Persistent 1-block leak per failed open.

Both are documented in the code itself for the eventual g-commits.
