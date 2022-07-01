# Ch 15 - A Bootloader That Reads From Disk

**Book pages:** 39-40 (Part 4)
**Code added:** `src/data.asm`, `src/boot.asm` rewritten, `build.sh` extended
**Test:** `tests/04-bootloader-reads-sector2.sh`

## What we built

A two-sector disk image:

```
sector 1 (offset 0x000 - 0x1FF) : boot.bin    - boot sector
sector 2 (offset 0x200 - 0x3FF) : data.bin    - "Hello World!!!" + zero padding
```

The boot sector runs at 0x7C00 with the same segment setup as Ch 8, then:

1. Calls BIOS INT 0x13 to read sector 2 into RAM at 0x7E00.
2. Loads SI with offset 0x200 (DS = 0x07C0, so that resolves to 0x7E00).
3. Calls `print` to walk the NUL-terminated string and emit one character at a time via INT 0x10.

## INT 0x13 register setup

```
AH = 0x02   (read sectors)
AL = 1      (one sector)
CH = 0      (cylinder 0)
CL = 2      (sector 2, 1-based)
DH = 0      (head 0)
DL = 0x80   (first HDD; QEMU's -hda)
ES:BX = 0x07C0:0x0200 = 0x7E00 (destination buffer)
```

## Building the disk image

```bash
nasm -f bin src/boot.asm -o bin/boot.bin
nasm -f bin src/data.asm -o bin/data.bin
cat bin/boot.bin bin/data.bin > bin/os.bin   # 1024 bytes total
```

The book uses `dd if=boot.bin of=os.bin` then `dd if=data.bin conv=notrunc oflag=append of=os.bin`. Plain `cat` does the same thing more concisely and we are not changing semantics.

## Why data.asm pads to 512 bytes

BIOS reads in whole sectors. If `data.bin` were smaller than 512 bytes, our disk image would have sector 2 ending early and BIOS might return whatever happens to be at offset 0x3FF for the last byte. The `times 512-($-$$) db 0` line guarantees a full sector.

## Quirk: INT 0x13 error handling is silent here

The book listing does not check the carry flag after `int 0x13`. If the read fails (no media, bad geometry, etc.) AL retains 0 sectors read and CF is set, but we still call `print` on whatever happens to be at 0x7E00 (possibly zeros or garbage from a previous boot). Real bootloaders check CF and retry or print an error. We follow the book verbatim and ignore it for now. This is a gotcha candidate for a later commit.

## Why the test got replaced

Same pattern as Ch 12 to Ch 13: each chapter's bootloader has its own primary observable behavior. The Ch 13 divide-by-zero test is no longer applicable because the new bootloader does not register a #DE handler.
