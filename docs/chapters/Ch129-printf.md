# Ch 135 - stdlib printf()

User-space `printf` supporting `%i` (integer) and `%s` (string). Built entirely on top of existing pieces: `putchar` for raw character, `print` for whole strings, `itoa` for int-to-string. Unknown format specifiers (anything other than `%i`, `%s`) print as-is.

## What landed

- `programs/stdlib/src/stdio.h` - prototype `int printf(const char* fmt, ...);`.
- `programs/stdlib/src/stdio.c`:
  - Pulls in `<stdarg.h>` for `va_list`, `va_start`, `va_arg`, `va_end` (provided by gcc's freestanding headers).
  - `printf` loop walks the format string; on `%`, switches on the next char.
- `programs/blank/blank.c` - calls `printf("My age is %i\n", 98)` before the existing print.

## Note on stdarg.h

We're compiling with `-ffreestanding -nostdlib -nodefaultlibs` but `<stdarg.h>` is a special header provided by the compiler itself (not by libc), so it remains available even in freestanding mode. That's exactly why varargs work here.

## Limitations (book-acknowledged)

No width/precision specifiers, no `%x`/`%d`/`%c`/`%f`, no return-value reporting of bytes written. Good enough for a kernel demo.

## Test impact

Still 32/32. VGA gains a "My age is 98" line above the prior output.
