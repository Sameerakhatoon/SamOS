# Ch 14 - Disk Access and How It Works

**Book pages:** 36-38 (Part 4)
**Code in this chapter:** none, prose

## Sectors are the disk's unit of access

Disks expose data in fixed-size blocks called sectors, classically 512 bytes. The disk hardware has no concept of files. Files are an OS construction layered on top of raw sectors via a filesystem driver.

We will write our own FAT16 driver many chapters from now. Until then, every disk read is "give me sector N".

## CHS addressing

Old-school disks were addressed by (Cylinder, Head, Sector) tuple:

- **Cylinder**: which concentric track on the platter
- **Head**: which platter surface
- **Sector**: which sector within that track

Modern drives use LBA (logical block address) which is just a linear sector index, but the BIOS INT 0x13 interface still takes CHS.

## BIOS INT 0x13, AH = 0x02 (read sectors)

| Register | Meaning |
|----------|---------|
| AH | 0x02 (read sectors function) |
| AL | number of sectors to read |
| CH | cylinder number (low 8 bits of 10-bit cylinder) |
| CL | sector number (1-based) in the low 6 bits |
| DH | head number |
| DL | drive number (0x80 = first HDD) |
| ES:BX | destination buffer |

After `INT 0x13`:
- CF (carry flag) = 0 means success.
- CF = 1 means failure; AH holds an error code.
- AL holds the number of sectors actually read.

Writing is similar with AH = 0x03 and ES:BX pointing at the source.

## Why this matters for us

The bootloader is 512 bytes (sector 1 on the disk). It is way too small to fit a kernel. So we use INT 0x13 in the boot sector to load additional sectors (the kernel) from disk into RAM, then jump to it.

The next chapter does the minimum version: read sector 2 into RAM and print whatever string lives there.
