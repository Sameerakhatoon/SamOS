#ifndef ARCH_AARCH64_PL011_H
#define ARCH_AARCH64_PL011_H

#include <stdint.h>
#include "firmware/fdt.h"

// Early console on an ARM PL011 UART.
//
// pl011_init finds the UART in the device tree: the /chosen
// stdout-path node if it is a PL011, else the first node compatible
// with "arm,pl011". With no usable device tree (f == NULL) or no
// PL011 in it, it falls back to 0x09000000, QEMU virt's UART, so a
// broken DTB can still be reported. Returns how the UART was found
// ("stdout-path", "compatible", "fallback").
const char* pl011_init(const struct fdt* f);
uint64_t    pl011_base(void);
void        pl011_putc(char c);
void        pl011_puts(const char* s);
// Receive interrupts on (RX and receive timeout), and the next
// byte received, -1 when the FIFO is empty (which clears them).
void        pl011_rx_enable(void);
int         pl011_getc(void);

#endif
