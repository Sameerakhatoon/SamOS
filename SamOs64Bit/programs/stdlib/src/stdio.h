#ifndef SAMOS_STDIO_H
#define SAMOS_STDIO_H

#include <stddef.h>
#include <stdarg.h>

int putchar(int c);
int puts(const char* s);

// %d %i %u %x %X %p %c %s %%, l/ll/z, '-' / '0' flags and a
// width. The snprintf family returns the length the full output
// would have had (C99), and always NUL-terminates when size > 0.
int printf(const char* fmt, ...);
int vprintf(const char* fmt, va_list ap);
int snprintf(char* buffer, size_t size, const char* fmt, ...);
int vsnprintf(char* buffer, size_t size, const char* fmt, va_list ap);
int sprintf(char* buffer, const char* fmt, ...);

#endif
