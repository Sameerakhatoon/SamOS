#ifndef ARCH_AARCH64_GICV3_H
#define ARCH_AARCH64_GICV3_H

#include <stdint.h>
#include "firmware/fdt.h"

// GICv3 interrupt controller, the aarch64 counterpart of the
// x86 LAPIC and IOAPIC (io/apic.h).
//
// INTIDs: 0-15 SGI (inter-processor), 16-31 PPI (per CPU: the timers),
// 32-1019 SPI (devices), 1020-1023 special (spurious). All interrupts
// are non-secure Group 1, priority 0xa0, SPIs routed to this CPU.
// The CPU interface is the system register one (ICC_*_EL1), not the
// GICv2 MMIO CPU interface.

#define GIC_INTID_SPURIOUS  1020    // ICC_IAR1_EL1 >= this: nothing to handle
#define GIC_INTID_PPI       16
#define GIC_INTID_SPI       32

// Find "arm,gic-v3" in the device tree, enable the distributor with
// affinity routing, wake this CPU's redistributor, enable the system
// register interface. 0, or -1 (printed) with no usable GICv3.
int      gic_init(const struct fdt* f);

void     gic_enable(uint32_t intid);
void     gic_disable(uint32_t intid);
uint32_t gic_ack(void);             // ICC_IAR1_EL1: INTID of the IRQ taken
void     gic_eoi(uint32_t intid);   // ICC_EOIR1_EL1: priority drop and deactivate

// INTID of entry `index` of `node`'s interrupts property, in the GIC's
// binding (<type number flags>, type 0 SPI, 1 PPI); -1 if absent.
int      gic_dt_intid(const struct fdt* f, int node, int index);

#endif
