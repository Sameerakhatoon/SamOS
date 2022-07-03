# Ch 18 - Summary of Real Mode

**Book page:** 45 (Part 4)
**Code in this chapter:** none, recap

## What Real Mode gave us

- A working bootloader at 0x7C00.
- BIOS API access via `INT 0x10` (video), `INT 0x13` (disk), `INT 0x16` (keyboard).
- Custom IVT entries: we registered our own ISR at vector 0x80 and an exception handler at vector 0.
- Disk reads via `INT 0x13`: read sector 2 into RAM and print it.
- MBR layout: 446 bytes of code, 64 bytes of partition table, 2 bytes of signature.

## What Real Mode does not give us

- More than 1 MB of addressable memory (20-bit address bus).
- Memory protection between code that runs on the same machine (no rings, no page faults).
- Virtual memory (no paging, no address translation).
- True multitasking (no preemption, no per-task address spaces).

These limits are why every serious OS leaves Real Mode quickly. Protected Mode (next part) gives us 32-bit addressing, ring 0/3 separation, segment descriptors with protection bits, and eventually paging.

## Where the next chapters take us

Part 5 starts with "What is Protected Mode?" then walks through:

1. Enabling the A20 line (so addresses above 1 MB do not wrap).
2. Building a Global Descriptor Table with code and data segments.
3. Setting the CR0.PE bit to switch the CPU mode.
4. Reloading segment registers and far-jumping into 32-bit code.

After that the book hands us our first chunks of real C code: a kernel entry point, a heap allocator, paging, an IDT, ATA driver, FAT16 driver, ELF loader, and so on.

## Reference

The book points to Ralf Brown's interrupt list at http://www.ctyme.com/intr/int.htm as the canonical reference for what BIOS interrupts exist and how they are invoked. Useful when we need to do something we have not seen yet in the bootloader.
