# Ch 4 - The Boot Process

**Book page:** 9-10 (Part 2 - The Basics)
**Code in this chapter:** none - prose

## The sequence

```
power on
  └── POST (hardware self-test by BIOS/UEFI firmware)
        └── BIOS/UEFI initializes devices, finds bootable disk
              └── first sector of disk loaded to 0x7c00 and jumped to (= our bootloader)
                    └── bootloader reads more sectors (the kernel) into RAM
                          └── bootloader jumps to the kernel
                                └── kernel initializes memory manager, drivers, scheduler
                                      └── kernel starts init / first user process
                                            └── system is "up"
```

## Where SamOs lives in this chain

The very first thing we write is the boot sector - the 512 bytes that BIOS loads at `0x7c00`. Everything from there forward is on us: BIOS hands us a 16-bit, segmented, no-protection environment with limited screen output via BIOS interrupts. The next several chapters walk that bootloader from "prints one character" to "loads our kernel from a later sector and jumps to it".

## Conventions BIOS gives us

- Code runs at `CS:0x7c00` in real mode (16-bit, segmented).
- The last two bytes of our 512-byte boot sector must be `0x55 0xAA` or BIOS refuses to boot.
- `INT 0x10` for screen output, `INT 0x13` for disk reads - both BIOS-provided.
- Interrupt vector table (IVT) sits at physical `0x0000` - 256 × 4-byte `(offset, segment)` entries.
