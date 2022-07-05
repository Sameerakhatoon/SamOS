# Ch 19 - What Is Protected Mode?

**Book pages:** 45-46 (Part 5)
**Code in this chapter:** none, prose

## The short version

Protected Mode is the second major operating mode introduced on x86 starting with the Intel 80286 (1982). It gives the CPU:

- 32-bit addressing on the 80386 and later (Real Mode had 20 bits).
- Per-segment access rights (read, write, execute, privilege level).
- Page-table-based virtual memory (on the 80386 and later).
- Hardware multitasking support (TSS, task switching).
- I/O permission bitmap (control which programs can use which I/O ports).

## Why we need it

Real Mode caps out at 1 MB of memory, has no protection, and gives us no way to isolate processes. Any serious OS leaves Real Mode shortly after the bootloader runs.

The transition is one-way at boot: once we enter Protected Mode, we usually do not go back to Real Mode (it is possible but rare). The bootloader handles the switch by setting up a minimal GDT, flipping the PE bit in CR0, and far-jumping to 32-bit code.

## What stays the same

The CPU still uses segment registers (CS, DS, SS, ES, FS, GS), but in Protected Mode they hold "selectors" that index into the GDT instead of acting as base addresses. The actual segment base and limit come from the GDT descriptor that the selector points to.

The IVT is replaced by the IDT (Interrupt Descriptor Table) which has a similar role but different entry format. We will set up the IDT in a later chapter.
