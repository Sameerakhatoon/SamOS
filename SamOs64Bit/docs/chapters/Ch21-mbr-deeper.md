# Ch 17 - Master Boot Record (MBR) Deeper Understanding

**Book pages:** 44-45 (Part 4)
**Code in this chapter:** none, prose

## Partition table layout

The 64-byte partition table sits at MBR offset 446 and holds exactly four 16-byte partition entries:

```
+--------+--------+--------+--------+
| PT[0]  | PT[1]  | PT[2]  | PT[3]  |
| 16B    | 16B    | 16B    | 16B    |
+--------+--------+--------+--------+
```

Each entry has roughly this layout (real x86 MBR partition entry):

| Offset (within entry) | Size | Field |
|-----------------------|------|-------|
| 0 | 1 byte | Boot indicator (0x80 = active, 0x00 = inactive) |
| 1-3 | 3 bytes | CHS address of first sector |
| 4 | 1 byte | Partition type (0x01 = FAT12, 0x06 = FAT16, 0x83 = Linux, ...) |
| 5-7 | 3 bytes | CHS address of last sector |
| 8-11 | 4 bytes | LBA of first sector |
| 12-15 | 4 bytes | Number of sectors |

## How BIOS picks which partition to boot

1. Read the MBR (sector 0 of the disk).
2. Scan the partition table for the entry with the active flag (byte 0 = 0x80).
3. Use that entry's "first sector" address to read sector 1 of the partition (called the Volume Boot Record).
4. Execute the VBR, which is itself a 512-byte program just like our MBR boot code.

Modern systems often replace this with chain-loading: the MBR boot code loads a more capable second-stage loader from a known location, which then handles the actual OS load.

## Our situation

SamOs is a single-image disk with no partition table populated. The 64 bytes between our code and the signature are zeros (set by `times 510-($-$$) db 0`). BIOS does not care because we are running off `qemu -hda` and BIOS just boots the MBR directly. Real hardware would still work because the signature is valid; it just would not see any "active partition" to chain into.
