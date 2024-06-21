#ifndef SAMOS_RISCV64_UART16550_H
#define SAMOS_RISCV64_UART16550_H

// NS16550A UART over MMIO, polled. QEMU virt has one at the
// address the device tree's /chosen stdout-path names.

#include <stdint.h>

// `reg_shift`: log2 of the register stride (0 on QEMU virt).
// `io_width`: 4 for 32-bit register accesses (the JH7110's
// DesignWare UART, reg-io-width 4), else bytes.
void uart16550_init(uintptr_t base, uint32_t reg_shift, uint32_t io_width);
void uart16550_putc(char c);
// Receive by interrupt: raise the UART's line when a byte
// arrives (ERBFI, and OUT2, which gates the line on PC-style boards).
void uart16550_enable_rx_irq(void);
// The next received byte, or -1 when the receive buffer is empty.
int uart16550_getc(void);

#endif
