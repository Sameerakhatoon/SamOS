# Ch 16 - Master Boot Record (MBR)

**Book pages:** 42-43 (Part 4)
**Code in this chapter:** none (book shows a template; not applied yet)

## MBR layout

A standard MBR is exactly 512 bytes laid out as:

```
+----------------------+---------+
| Boot code            | 446 B   |  bytes 0..445
+----------------------+---------+
| Partition table      |  64 B   |  bytes 446..509, four 16-byte entries
+----------------------+---------+
| Boot signature       |   2 B   |  bytes 510..511, must be 0x55 0xAA
+----------------------+---------+
```

Our bootloader so far follows the first and third columns (boot code plus signature) but has no partition table. For QEMU's `-hda` mode that does not matter, the firmware only checks for the 0xAA55 signature at bytes 510-511 to decide whether to boot.

## The BPB prologue convention

The book shows a "minimalist MBR" with this header:

```asm
ORG 0x7c00
jmp short start
nop
start:
    ; bootloader code
```

The `jmp short` followed by `nop` is exactly 3 bytes. Later in the book when we add a FAT16 filesystem, the bytes immediately after that 3-byte stub need to hold the BIOS Parameter Block (BPB), which describes the filesystem geometry. Many real-world bootloaders embed the BPB right at offset 3 so disk tools recognize the volume.

We are not adding the BPB right now. Our current bootloader still works because BIOS only looks at the signature, not the partition table or any BPB header.

## When the partition table will matter

Real PCs typically have a partitioned disk. The BIOS reads the MBR, scans the four partition entries for the one marked "active", then loads the boot sector of that partition (a Volume Boot Record, VBR). Our QEMU disk image has only one "partition" (the whole image) so we are running the simplest possible setup. Adding partition entries is optional; we likely will not until much later if at all.
