#ifndef KERNEL_IRQ
#define KERNEL_IRQ

// Lecture 67 - IRQ enable/disable C API.
//
// The 8259 master PIC handles IRQ0..IRQ7; the slave PIC handles
// IRQ8..IRQ15 and cascades through master IRQ2. The mask bit
// for an IRQ lives in:
//   master: port 0x21, bit IRQ
//   slave:  port 0xA1, bit (IRQ - 8)
// where bit set = masked (disabled), bit clear = unmasked
// (enabled).
//
// The IRQ numbers are in io/isa.h, and the rest of the kernel
// calls arch_irq_unmask / arch_irq_mask (arch/arch.h).

#include "io/isa.h"

#define PIC_MASTER_ENDING_IRQ   7
#define PIC_SLAVE_STARTING_IRQ  8
#define PIC_SLAVE_ENDING_IRQ    15
#define IRQ_MASTER_PORT         0x21    // master data port
#define IRQ_SLAVE_PORT          0xA1    // slave data port

typedef int IRQ;

void IRQ_enable(IRQ irq);
void IRQ_disable(IRQ irq);

#endif
