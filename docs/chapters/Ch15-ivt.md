# Ch 11 - The Interrupt Vector Table Explained

**Book pages:** 28-30 (Part 4)
**Code in this chapter:** none, prose

## Layout

In real mode the IVT lives at physical `0x00000000` and is exactly 1024 bytes:

```
256 entries x 4 bytes each = 1024 bytes = 1 KiB
```

Each 4-byte entry is a far pointer to the ISR:

```
offset 0..1 : OFFSET  (16-bit, low half first)
offset 2..3 : SEGMENT (16-bit)
```

The CPU combines them with `(SEGMENT << 4) + OFFSET` to land on the first instruction of the ISR (same address formula as ordinary segmented memory access).

## Looking up a vector

For interrupt number `N` the table offset is `N * 4`:

```
INT 0x10  ->  IVT entry at offset 0x40   (0x10 * 4)
INT 0x20  ->  IVT entry at offset 0x80   (0x20 * 4)
INT 0x80  ->  IVT entry at offset 0x200  (0x80 * 4)
```

## What firing an interrupt does (real mode)

1. CPU reads the IVT entry at `(N * 4)`.
2. Pushes FLAGS, then CS, then IP onto the stack.
3. Clears the IF flag (interrupts disabled inside the handler unless the handler re-enables them).
4. Loads CS and IP from the IVT entry.
5. Starts executing the handler.

`IRET` undoes that: pop IP, pop CS, pop FLAGS, resume.

## BIOS owns most vectors at boot

When BIOS hands control to our boot sector, it has already populated dozens of IVT entries with pointers to its own ISRs (`INT 0x10` video, `INT 0x13` disk, `INT 0x16` keyboard, etc.). We have used those for free. The next chapter shows how to overwrite an entry with our own handler.

## Why the IVT goes away later

Protected Mode uses a different structure called the IDT (Interrupt Descriptor Table). The IDT entries are bigger (8 bytes each) and richer (privilege level, gate type, present flag). We will build the IDT once we get to 32-bit code. Until then, the simpler real-mode IVT at physical 0 is what we work with.
