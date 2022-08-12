# Ch 38 - Implementing Text Mode

**Book pages:** 108-110 (Part 5)
**Code added:** real `kernel.c` with VGA terminal driver, `VGA_*` constants in `kernel.h`
**Test:** `tests/10-vga-prints-hello.sh`

## What we built

The kernel now writes directly to VGA text RAM at physical 0xB8000. After `kernel_main` runs the screen shows:

```
Hello world!
test
```

starting at row 0.

## kernel.h additions

```c
#define VGA_WIDTH  80
#define VGA_HEIGHT 20
```

The book sets `VGA_HEIGHT` to 20. The actual VGA text-mode resolution is 80x25; we follow the book number verbatim. If we ever try to print past row 19 we just stop using rows 20-24, no harm done.

## kernel.c module breakdown

State:

```c
uint16_t* video_mem = 0;
uint16_t  terminal_row = 0;
uint16_t  terminal_col = 0;
```

A pointer to VGA RAM (assigned at init) plus the current cursor (row, col).

`terminal_make_char(c, colour)` packs a character + attribute byte into a single 16-bit value, in the format the VGA hardware expects (`(colour << 8) | char`).

`terminal_putchar(x, y, c, colour)` writes a single cell at (x, y).

`terminal_writechar(c, colour)` is the cursor-advancing version. Handles `\n` by moving to the start of the next row.

`terminal_initialize()` clears the screen by writing space characters with attribute 0 to every cell, then resets the cursor to (0, 0).

`strlen` and `print` are the obvious classics: `strlen` counts bytes until NUL, `print` walks a string calling `terminal_writechar` for each character with attribute 15 (white on black).

`kernel_main` calls `terminal_initialize` then `print("Hello world!\ntest")`.

## Why use uint16_t* not uint8_t*

VGA stores cells as (char, attribute) pairs. By treating video memory as `uint16_t*` we write both bytes per cell in one store. The low byte (ASCII) and high byte (attribute) end up in the correct order because little-endian x86 stores the low byte at the lower address.

## Why this is the first visible thing

Until this chapter the kernel ran, but produced no observable effect except the CPU register state. With a working VGA driver:

- We can debug by printing values to the screen.
- Future error handlers can show what went wrong before halting.
- The user (us) gets visual feedback that the boot pipeline actually works end to end.

## How the test verifies it

Same VGA-buffer-grep approach used by the early real-mode tests. Boot the image, wait 2 seconds, ask QEMU monitor for the 4 KiB of memory starting at 0xB8000, strip the attribute bytes (every other byte), grep for "Hello world!" and "test".

## Quirks worth noting

- We do not handle scrolling. Once 20 rows are filled, subsequent writes silently overrun rows we never explicitly cleared (BIOS may have left text there). Scrolling is a later chapter (or a self-imposed extra).
- `colour` is `char` not `uint8_t`. The C type promotion of `(colour << 8) | c` ends up correct because the shift produces an `int` and the `|` widens `c`. We are not relying on subtle behavior.
- `video_mem` starts as `0`. If `kernel_main` runs before `terminal_initialize()` and tries to use the terminal, it dereferences NULL. There is no NULL check; we trust the call order.
