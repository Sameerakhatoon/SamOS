#ifndef SAMOS_RISCV64_PLIC_H
#define SAMOS_RISCV64_PLIC_H

// The platform-level interrupt controller, riscv64's IOAPIC.
//
// Device interrupt n (the device tree's `interrupts` cell) has a
// priority; each context (one per hart and privilege mode) has an
// enable bit per source and a priority threshold. An enabled source
// above the threshold raises the context's external interrupt (SEIP
// for an S-mode context); the handler claims the highest one, serves
// it and writes the id back to complete it. Sources are level
// triggered at the device: completing a still-asserted line raises it
// again.

#include <stdbool.h>
#include "arch/riscv64/trap.h"

struct fdt;

// Find the PLIC in the device tree, mask every source, pick this
// hart's S-mode context (from interrupts-extended), threshold 0, and
// enable SEIE. `cpu` is the boot hart's /cpus node. 0 or -1.
int  plic_init(const struct fdt* f, int cpu);
// The handler runs in interrupt context, before the source completes.
int  plic_register_handler(unsigned irq, INTERRUPT_CALLBACK_FUNCTION handler);
void plic_enable(unsigned irq);
void plic_disable(unsigned irq);
// This hart's S-mode context (into arch.plic_ctx): nothing
// enabled, threshold 0, SEIE. The context, or -1; plic_init does the
// boot hart's.
int  plic_init_hart(const struct fdt* f, int cpu);
// Deliver `irq` to context `ctx` (another hart's) from now on.
int  plic_route(unsigned irq, int ctx);
// Supervisor external interrupt: claim, dispatch, complete until the
// claim register reads 0.
void plic_handle(struct trap_frame* frame);
// Interrupts served for `irq` so far.
unsigned long plic_count(unsigned irq);

#endif
