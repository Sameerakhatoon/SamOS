# Ch 37 - Understanding Text Mode in Protected Mode

**Book pages:** 106-107 (Part 5)
**Code in this chapter:** none, prose

## What text mode is

A display mode where the screen is a grid of character cells rather than a grid of pixels. Each cell holds one character plus an attribute byte (foreground color, background color, blink). Standard VGA text mode is 80 columns wide and 25 rows tall, totaling 2000 cells.

In Real Mode we did not have to touch the hardware to print: BIOS `INT 0x10, AH=0x0E` did teletype output for us. That ride is over now that we are in Protected Mode.

## Why BIOS is no longer available

BIOS routines live in 16-bit Real Mode code at fixed addresses. When the CPU is in Protected Mode the segment selectors mean something different. CS no longer holds a base address; it indexes the GDT. Jumping into BIOS code would either trap (because the GDT entry for that region forbids execution) or run garbage because the instructions would be decoded as 32-bit.

Some kernels add a "v86 mode" task to drop briefly back into a virtual 8086 to call BIOS. SamOs will not. We just talk to the hardware directly from now on.

## The VGA text buffer

Video memory for text mode lives at physical address `0xB8000` and is 4 KiB (covers the visible 80x25 grid plus some). Each character cell is 2 bytes:

```
byte 0: ASCII code of the character
byte 1: attribute  (bits 0..3 = foreground color, bits 4..6 = background color, bit 7 = blink)
```

Writing to `*(volatile uint16_t*)0xB8000 = 0x0F41` puts a white `A` (ASCII 0x41) at the top-left cell. White on black is attribute 0x0F. Color codes 0..15 follow the standard CGA palette (0=black, 1=blue, ..., 15=white).

## What the next chapter builds

A small terminal abstraction:

- `terminal_initialize()`: clears the screen, resets cursor to (0,0).
- `terminal_putchar(x, y, c, color)`: writes a single character at (x, y).
- `terminal_writechar(c, color)`: writes a character at the current cursor and advances.
- `print(const char*)`: writes a NUL-terminated string.
- `strlen(const char*)`: classic length-of-string.

Plus `kernel_main` calling `terminal_initialize()` and `print("Hello world!\ntest")`.
