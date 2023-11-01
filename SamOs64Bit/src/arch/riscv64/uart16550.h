#ifndef SAMOS_RISCV64_UART16550_H
#define SAMOS_RISCV64_UART16550_H

// NS16550A UART over MMIO, polled. QEMU virt has one at the
// address the device tree's /chosen stdout-path names.

#include <stdint.h>

// `reg_shift`: log2 of the register stride (0 on QEMU virt).
void uart16550_init(uintptr_t base, uint32_t reg_shift);
void uart16550_putc(char c);

#endif
