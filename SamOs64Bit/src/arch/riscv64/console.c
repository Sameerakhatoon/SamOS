// Early console with a minimal printf.

#include <stdarg.h>
#include <stdint.h>
#include "arch/riscv64/console.h"
#include "arch/riscv64/sbi.h"

static void (*console_out)(char c) = sbi_console_putc;

void console_set_putc(void (*putc)(char c))
{
    console_out = putc;
}

void console_putc(char c)
{
    if (c == '\n') {
        console_out('\r');
    }
    console_out(c);
}

void console_puts(const char* s)
{
    while (*s) {
        console_putc(*s++);
    }
}

static void put_unsigned(uint64_t v, unsigned base)
{
    char buf[24];
    int i = 0;
    do {
        buf[i++] = "0123456789abcdef"[v % base];
        v /= base;
    } while (v);
    while (i) {
        console_putc(buf[--i]);
    }
}

void console_printf(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            console_putc(*fmt);
            continue;
        }
        fmt++;
        int is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }
        switch (*fmt) {
        case 's': console_puts(va_arg(ap, const char*)); break;
        case 'c': console_putc((char)va_arg(ap, int)); break;
        case 'd': {
            int64_t v = is_long ? va_arg(ap, int64_t) : va_arg(ap, int);
            if (v < 0) {
                console_putc('-');
                v = -v;
            }
            put_unsigned((uint64_t)v, 10);
            break;
        }
        case 'u': put_unsigned(is_long ? va_arg(ap, uint64_t) : va_arg(ap, unsigned), 10); break;
        case 'x': put_unsigned(is_long ? va_arg(ap, uint64_t) : va_arg(ap, unsigned), 16); break;
        case '%': console_putc('%'); break;
        case 0: va_end(ap); return;
        default: console_putc('%'); console_putc(*fmt); break;
        }
    }
    va_end(ap);
}
