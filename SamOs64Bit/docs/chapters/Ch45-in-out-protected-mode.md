# Ch 41 - Understanding IN and OUT in Protected Mode

**Book pages:** 124-125 (Part 5)
**Code in this chapter:** none, prose

## What IN and OUT do

x86 has two address spaces: memory and I/O. Memory you access with normal `mov`. I/O ports you access with `in` and `out`. Devices are attached to specific port numbers (the PIC at 0x20 and 0xA0, the keyboard controller at 0x60 and 0x64, ATA at 0x1F0-0x1F7, etc.).

```
in  al, dx       ; read 1 byte from port DX into AL
in  ax, dx       ; read 2 bytes
in  eax, dx      ; read 4 bytes

out dx, al       ; write 1 byte from AL to port DX
out dx, ax       ; write 2 bytes
out dx, eax      ; write 4 bytes
```

A port can also be specified as an 8-bit immediate (`in al, 0x60`) if it fits in one byte.

## Why this is the same in Real Mode and Protected Mode

The instructions themselves are mode-agnostic. The address space is 16 bits wide (so port numbers go 0..65535) regardless of CPU mode. The bootloader already used `out 0x92, al` to enable the A20 line in Protected Mode without any extra setup.

## What changes in Protected Mode

One subtlety: the `IOPL` field of EFLAGS controls which privilege levels can execute `in`/`out`. The CPU compares the current CPL with IOPL. If `CPL <= IOPL`, the instruction runs; otherwise it raises a `#GP`.

EFLAGS.IOPL defaults to 0, which means only ring 0 can do I/O. That is what we want: only the kernel touches hardware ports. User-mode programs are not even given the chance.

We can also opt in finer-grained access through the TSS's I/O permission bitmap, but the default deny-all-from-ring-3 policy is what every modern OS uses.

## Why we need a C wrapper

C has no syntax for `in`/`out`. We could use GCC's inline assembly (`asm volatile (...)`) but that gets cluttered when you need it in many places. The book's approach is a tiny `io.asm` with four wrappers: `insb`, `insw`, `outb`, `outw`. C calls them like normal functions.

The next chapter implements those four wrappers.
