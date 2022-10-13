# Ch 79 - FAT16 read

Book Ch 75: the final wire-up. The FAT16 driver gets a `read`
function, the volume gets a real file in it, and the kernel reads
that file end-to-end.

## What got added

- `src/fs/fat/fat16.c`:
  - Forward-decl `int fat16_read(...)` next to the other top-of-file
    function prototypes.
  - Real `fat16_read`: pull the `fat_file_descriptor` out of `private`,
    grab its underlying `fat_directory_item`, and call
    `fat16_read_internal(disk, fat16_get_first_cluster(item),
    pos, size, out_ptr)` once per `nmemb`. Bump `out_ptr` and the
    running offset by `size` each iteration. Return `nmemb` on
    success, or the negative `fat16_read_internal` error code.
  - `struct filesystem fat16_fs` now also sets `.read = fat16_read`,
    so the VFS dispatch table actually has somewhere to send the
    `fread` call.
- `Makefile` - two new lines after the 16 MiB pad:
  ```
  mformat -k -R 200 -c 4 -r 64 -i ./bin/os.bin > /dev/null
  mcopy -i ./bin/os.bin ./hello.txt ::/hello.txt > /dev/null
  ```
  Flags:
  - `-k` keeps the boot sector code (our `jmp short start` + boot
    code lives intact). OEM identifier and SystemID string survive.
  - `-R 200` sets reserved sectors to 200 so the kernel image
    (which the boot loader reads from sectors 1..100) stays inside
    the FAT16 reserved region instead of clobbering FAT entries.
  - `-c 4` sets sectors per cluster (2 KiB clusters) to keep the
    cluster count in FAT16 range; `-c 128` would have given FAT12
    on a 16 MiB volume.
  - `-r 64` requests 64 root-dir entries (mformat may round up to
    fill a sector).
- `src/kernel.c`:
  - Replaced the `fop_miss` probe with a real open + read:
    ```c
    int fd = fopen("0:/hello.txt", "r");
    if(fd){
        char hbuf[14];
        fread(hbuf, 13, 1, fd);
        hbuf[13] = 0;
        print("\nh="); print(hbuf);
    } else {
        print("\nh=NOPEN");
    }
    ```
  - Expected on-screen `h=Hello world!`.

## Test surgery

- `tests/20-fat16-bpb.sh` - dropped the SectorsPerCluster check.
  mformat -k preserves the OEM ID + SystemID string but rewrites the
  numeric BPB fields, so the surviving invariants are OEM,
  BytesPerSector, SystemID, and the boot signature.
- `tests/26-fat16-resolver.sh` - root directory total now = 1
  (the hello.txt entry mformat dropped in), not 0.
- `tests/13-kmalloc-works.sh` - heap math reflects the bigger root dir
  buffer (4 KiB blocks instead of 1) plus the persistent fopen/fread
  allocations. New first-fit slot is 1045, so `km1 = 0x01415000`.
- `tests/30-fat16-open-miss.sh` deleted - the kernel no longer runs
  the "no such file" probe (replaced with the hello.txt probe).
- `tests/32-fat16-read.sh` added - greps the VGA buffer for
  `h=Hello world!`.
- Every VGA dump test bumped from `sleep 7` to `sleep 10`. The
  longer-running `fat16_resolve` (now reading a 16 KiB root directory
  via the streamer) plus `make clean` rebuilds in `build.sh` push
  the kernel's pre-probe time past the old budget.

## What "Hello world!" on screen proves

End to end, the chapter exercises every layer we've built:

1. Boot loader reads kernel.bin off ATA into RAM.
2. Paging is enabled; the BPB is read by the disk streamer through
   `disk_read_block`.
3. FAT16 resolver validates the EBPB signature, allocates
   `fat_private`, and walks the now-populated root directory.
4. Path parser splits `"0:/hello.txt"` into drive + segments.
5. VFS dispatches to `fat16_open`, which walks the root dir, finds
   `HELLO.TXT` via `istrncmp`, clones its directory item.
6. VFS dispatches to `fat16_read`, which traces the cluster chain,
   streams the data via `diskstreamer_read`, hands back 13 bytes.
7. Kernel `print`s the bytes, the VGA buffer holds them, the test
   dumps the buffer and greps for the string.

If any link in that chain is broken, `tests/32-fat16-read.sh` fails.

## Known book bugs still present

The two bugs noted in Ch 72 still need g-commits:

- `fat16_new_fat_item_for_directory_item` unconditionally sets
  `type = FAT_ITEM_TYPE_FILE`. Doesn't bite yet because the smoke
  probe path doesn't traverse a subdirectory; will when it does.
- `fat16_open` early-returns `ERROR(-EIO)` without `kfree`ing the
  descriptor it just kzalloc'd. 1-block leak per failed open.
