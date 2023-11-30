// Early console printf on the PL011, the counterpart of
// riscv64/console.c, with a zero-padded width for register dumps.
// Lines from different threads stay whole if the caller masks IRQs
// around them (console_lock).

#include <stdarg.h>
#include <stdint.h>
#include "arch/aarch64/console.h"
#include "arch/aarch64/pl011.h"

static void put_unsigned(uint64_t v, unsigned base, int width)
{
    char buf[24];
    int i = 0;
    do {
        buf[i++] = "0123456789abcdef"[v % base];
        v /= base;
    } while (v);
    while (i < width && i < (int)sizeof buf)
        buf[i++] = '0';
    while (i)
        pl011_putc(buf[--i]);
}

void console_printf(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            pl011_putc(*fmt);
            continue;
        }
        fmt++;
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + *fmt++ - '0';
        int is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }
        switch (*fmt) {
        case 's': pl011_puts(va_arg(ap, const char*)); break;
        case 'c': pl011_putc((char)va_arg(ap, int)); break;
        case 'd': {
            int64_t v = is_long ? va_arg(ap, int64_t) : va_arg(ap, int);
            if (v < 0) {
                pl011_putc('-');
                v = -v;
            }
            put_unsigned((uint64_t)v, 10, width);
            break;
        }
        case 'u': put_unsigned(is_long ? va_arg(ap, uint64_t) : va_arg(ap, unsigned), 10, width); break;
        case 'x': put_unsigned(is_long ? va_arg(ap, uint64_t) : va_arg(ap, unsigned), 16, width); break;
        case '%': pl011_putc('%'); break;
        case 0: va_end(ap); return;
        default: pl011_putc('%'); pl011_putc(*fmt); break;
        }
    }
    va_end(ap);
}
