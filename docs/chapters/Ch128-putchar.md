# Ch 134 - stdlib putchar()

Adds the C-standard `putchar()` to our stdlib. No kernel changes - reuses the existing SYSTEM_COMMAND3_PUTCHAR syscall from Ch 117.

## What landed

- `programs/stdlib/src/samos.asm` - new `samos_putchar(c)` routine wrapping `int 0x80` with `eax = 3`.
- `programs/stdlib/src/samos.h` - prototype.
- `programs/stdlib/src/stdio.{h,c}` - new translation unit with `int putchar(int c)` calling `samos_putchar((char)c)`.
- `programs/stdlib/src/stdlib.h` - include guard renamed `STDLIB_H` -> `SAMOS_STDLIB_H` per book's chapter note about scoping the standard library to our kernel.
- `programs/stdlib/Makefile` - assembles `stdio.o` and links it.
- `programs/blank/blank.c` - calls `putchar('Z')` between the itoa print and malloc.

## Test impact

Suite stays 32/32. The visible difference is a `Z` on the VGA after `8763`.
