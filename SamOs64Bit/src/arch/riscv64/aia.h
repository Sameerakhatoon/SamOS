#ifndef SAMOS_RISCV64_AIA_H
#define SAMOS_RISCV64_AIA_H

// Parity (riscv64 PCI) - the Advanced Interrupt Architecture, what
// QEMU virt has instead of the PLIC with aia=aplic-imsic.
//
// Each hart has an IMSIC interrupt file: a message (a 32-bit write of
// an identity, EIID, to the hart's page) sets that identity pending;
// an enabled one above the threshold raises the supervisor external
// interrupt, and stopei claims the highest. PCI MSI and MSI-X write
// there directly. Wired lines go to the APLIC, which in MSI delivery
// mode turns source n into the message EIID n to the hart its target
// register names. Sources are level-triggered: when the device still
// asserts the line after the handler, the source is set pending again
// (setipnum, AIA spec 4.9.2) and sends another message.
//
// EIIDs: 1-127 the APLIC's sources, 128 + slot the MSI of a driver's
// completion slot (arch_irq_msi). Every hart enables all of them.

#include <stdbool.h>
#include <stdint.h>
#include "arch/riscv64/trap.h"

struct fdt;
struct cpu;

// Find the S-level IMSIC and APLIC in the device tree; the APLIC in
// MSI mode, every source off; this hart's interrupt file. 0 or -1.
int  aia_init(const struct fdt* f, int cpu);
bool aia_active(void);
// The interrupt file of the hart whose /cpus node is `cpu` (this one):
// delivery on, threshold 0, every EIID enabled, SEIE. Into
// arch.imsic_hart; its index, or -1.
int  aia_init_hart(const struct fdt* f, int cpu);
// APLIC source `src`, level-high, to the boot hart.
int  aia_register_handler(unsigned src, INTERRUPT_CALLBACK_FUNCTION handler);
void aia_enable(unsigned src);
void aia_disable(unsigned src);
int  aia_route(unsigned src, struct cpu* c);
// The message for completion slot `slot` on CPU `c` (arch_irq_msi).
int  aia_msi(unsigned slot, struct cpu* c, INTERRUPT_CALLBACK_FUNCTION handler,
             uint64_t* addr, uint32_t* data);
// Supervisor external interrupt: claim and dispatch until none is left.
void aia_handle(struct trap_frame* frame);
unsigned long aia_count(unsigned eiid);

#endif
