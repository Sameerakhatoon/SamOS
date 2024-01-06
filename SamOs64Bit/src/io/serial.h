#ifndef SERIAL_H
#define SERIAL_H

// COM1 serial console (16550 UART at port 0x3F8).
//
// Not in the book. Everything print() writes, and everything a
// user process prints, is mirrored here so headless tests (and
// later the ARM64 / RISC-V ports, which start serial-only) can
// read kernel output without the framebuffer. QEMU exposes it via
// `-serial stdio` or `-serial file:PATH`.

#define SERIAL_COM1_PORT 0x3F8

void serial_init(void);
void serial_putc(char c);
void serial_write(const char* str);
void serial_write_hex(unsigned long long value);
// Send this CPU's buffered partial line; lock-free writers
// for the panic path.
void serial_flush(void);
void serial_write_unlocked(const char* str);
void serial_write_hex_unlocked(unsigned long long value);

#endif
