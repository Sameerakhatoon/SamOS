# Ch 31 - LBA vs CHS

**Book pages:** 75-76 (Part 5)
**Code in this chapter:** none, prose

## CHS in one sentence

"Read the sector at (Cylinder C, Head H, Sector S)." The address maps directly to a physical location on a spinning platter, which made sense in the 1980s when disks really did have cylinders, heads, and platters.

Downsides:
- Three numbers, not one.
- Each field is fixed-size and small. The original CHS could address only about 8 GB.
- SSDs have no cylinders or heads. The numbers are fiction the firmware translates back to its own flash layout, so CHS adds a useless indirection.

We used CHS in Ch 15 with BIOS INT 0x13 because that interface only accepts CHS.

## LBA in one sentence

"Read sector number N." N starts at 0 for the first sector and just counts up. Modern LBA addresses are 48 bits wide, which covers 128 PiB.

This is what ATA's PIO read command (0x20) uses. Our `ata_lba_read` routine puts the sector index in a 28-bit LBA spread across ports 0x1F3 (byte 0), 0x1F4 (byte 1), 0x1F5 (byte 2), and the low 4 bits of 0x1F6 (byte 3). 28 bits gives us 128 GiB of address range, which is plenty for our toy disk.

## Why both still exist

- Real Mode BIOS INT 0x13 ah=0x02 wants CHS for legacy reasons. BIOS Int 0x13 extensions (ah=0x42) accept LBA but require the call to use a packet structure, and many bootloaders avoid that complexity for the boot sector.
- Once we are in Protected Mode and talking directly to ATA, LBA is the only sensible choice.

So in practice: CHS in the boot sector (one BIOS call), LBA everywhere after. The next chapter implements LBA reads in protected mode.
