# Ch 6 - Understanding Real Mode

**Book pages:** 17-18 (Part 4)
**Code in this chapter:** none, prose

## What Real Mode is

x86 CPUs power on in Real Mode. It mimics the old 8086/8088 environment:

- 16-bit registers, 20-bit physical address bus, 1 MB of addressable memory.
- Single-tasking. No memory protection, no virtual memory, no privilege rings.
- Direct hardware access via I/O ports or memory-mapped regions.
- BIOS API available through software interrupts (the bootloader uses `INT 0x10` for screen output, `INT 0x13` for disk reads).

## Why we start here

Every x86 boot begins in Real Mode. The bootloader has to live with the constraints (16-bit registers, segment:offset addressing, no protection) until we explicitly switch to Protected Mode later in the book.

## BIOS API in one line

A set of pre-installed interrupt handlers that expose screen, keyboard, disk, and timer services. Saves us writing drivers in the bootloader. Examples we'll use:

- `INT 0x10`: video services (teletype output, scroll, set cursor)
- `INT 0x13`: disk services (read sectors, reset controller)
- `INT 0x16`: keyboard services

The bootloader from the previous chapter calls `INT 0x10, AH=0x0E` once per character to print "Hello, World!".
