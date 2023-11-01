#ifndef SAMOS_RISCV64_CONSOLE_H
#define SAMOS_RISCV64_CONSOLE_H

// Early console. Starts on the SBI console; console_set_putc
// moves it to a UART once the device tree has named one.

void console_set_putc(void (*putc)(char c));
void console_putc(char c);
void console_puts(const char* s);
// %s %c %d %u %x with an optional l (64-bit), and %%. No widths.
void console_printf(const char* fmt, ...);

#endif
