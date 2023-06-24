# Ch 58 - Implementing the disk driver in C

Book Ch 54: turn the Ch 53 port-map prose into actual code we can call
from the kernel. After this we can read raw sectors off the disk without
going through the BIOS.

## What got added

- `src/disk/disk.h` - exports a single function:
  `int disk_read_sector(int lba, int total, void* buf);`
- `src/disk/disk.c` - PIO read of `total` 512-byte sectors starting at
  LBA `lba`, into `buf`. Steps:
  1. Write the top 4 bits of the LBA into 0x1F6, OR'd with 0xE0 (LBA
     mode + master drive).
  2. Write sector count to 0x1F2, low/mid/high LBA bytes to
     0x1F3/0x1F4/0x1F5.
  3. Write 0x20 (READ SECTORS) to 0x1F7.
  4. For each sector, poll 0x1F7 until DRQ (bit 3, mask 0x08) is set,
     then `insw` 256 times from 0x1F0 into the buffer.
- `Makefile` - added `./build/disk/disk.o` to `FILES` and the build rule.
- `build.sh` - added `build/disk` to the `mkdir -p` line.

## What got removed (per book)

The book explicitly tells us to delete the Ch 52 remap demo from
`kernel.c` before adding the disk smoke probe ("its important to keep
our kernel clean at all times"). Done:

- `char* ptr = kzalloc(4096);` plus the `paging_set` call and the
  `ptr2[0]='A'; ptr2[1]='B'; print(ptr2); print(ptr);` lines are gone.
- `tests/15-paging-remap.sh` deleted, since the on-screen `remap:ABAB`
  it grepped for is no longer produced. The Ch 52 commit still ships
  test 15 in git history, so that chapter's evidence stays intact.
- The kmalloc-smoke heap-block accounting in `tests/13-kmalloc-works.sh`
  reverts to 1025 blocks pre-`kernel_main`-probe, so the expected
  addresses go back to `0x01402000`/`0x01403000`/`0x01402000`.

## The smoke probe

In `kernel.c`, just before `enable_interrupts()`:

```c
char buf[512];
disk_read_sector(0, 1, buf);
print("\nbootsig=");
print_hex32(((unsigned int)(unsigned char)buf[510] << 8)
            | (unsigned char)buf[511]);
```

We read LBA 0 (the boot sector that QEMU loaded from `bin/os.bin`) and
print the last two bytes. Any valid PC boot sector ends in `0x55 0xAA`,
so the screen should show `bootsig=000055AA`. `tests/16-disk-reads-sector.sh`
greps for exactly that.

## Why the buffer ends `55 AA`

Test 1 (`02-bootloader-prints-hello.sh`) already asserts our
`bin/boot.bin` ends with `55 AA`. Since `build.sh` writes `boot.bin` as
the first 512 bytes of `bin/os.bin`, reading sector 0 off `os.bin`
must produce those same bytes at offsets 510 and 511. If
`disk_read_sector` botches the LBA encoding, the DRQ wait, the `insw`
byte order, or the 256-vs-512-word transfer count, the boot signature
will land somewhere other than offsets 510..511 and test 16 will fail.
