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

// Device lines (arch_irq_attach_gsi) and IPIs. Priorities are
// GIC values (lower is more urgent); the SPI routing and the SGI
// target take an MPIDR_EL1 value.
void     gic_set_priority(uint32_t intid, uint8_t priority);
void     gic_set_edge(uint32_t intid, int edge);     // SPIs: edge (1) or level (0)
void     gic_route(uint32_t intid, uint64_t mpidr);
void     gic_send_sgi(uint32_t sgi, uint64_t mpidr);
// The flags cell of `node`'s interrupt `index` (1 edge rising, 4 level
// high, ...), 0 if absent.
uint32_t gic_dt_flags(const struct fdt* f, int node, int index);

// SMP. Each CPU has its own redistributor (SGIs, PPIs): struct
// arch_cpu's gicr, from gic_redistributor(its MPIDR) (0: none), set
// up by gic_init_cpu on that CPU. gic_fiq_sgi makes `sgi` a Group 0
// (FIQ) interrupt at the top priority on this CPU, 0 if Group 0 is not
// the kernel's; gic_send_fiq_others sends it to every other CPU.
uint64_t gic_redistributor(uint64_t mpidr);
int      gic_init_cpu(void);
int      gic_fiq_sgi(uint32_t sgi);
void     gic_send_fiq_others(uint32_t sgi);

// S-PAR - LPIs (its.c): INTIDs GIC_INTID_LPI.. for MSIs, GIC_LPIS of
// them dispatched (trap.c). The redistributor frames gic_init found,
// for their LPI tables: gic_rd_frame(0 .. gic_rd_count() - 1).
#define GIC_INTID_LPI       8192
#define GIC_LPIS            64
int      gic_rd_count(void);
uint64_t gic_rd_frame(int i);
// The priority mask (ICC_PMR_EL1 on GICv3) for a disk wait.
uint32_t gic_get_pmr(void);
void     gic_set_pmr(uint32_t pmr);

// The same calls on another controller, which gic_init picks
// from the device tree when it has no "arm,gic-v3": a GICv2 (GIC-400,
// gicv2.c: Pi 4 and 5, QEMU virt,gic-version=2) or the BCM2836 per-core
// controller (bcm2836_intc.c: Pi 3, QEMU raspi3b). INTIDs keep the
// GIC's numbering. cpu_token is gic_redistributor's answer there (0: no
// such CPU); a 0 dt_intid uses the GIC binding. No FIQ stop on either.
struct gic_ops {
    const char* name;
    int      (*init_cpu)(void);
    void     (*enable)(uint32_t intid);
    void     (*disable)(uint32_t intid);
    uint32_t (*ack)(void);
    void     (*eoi)(uint32_t intid);
    int      (*dt_intid)(const struct fdt* f, int node, int index);
    uint32_t (*dt_flags)(const struct fdt* f, int node, int index);
    void     (*set_priority)(uint32_t intid, uint8_t priority);
    void     (*set_edge)(uint32_t intid, int edge);
    void     (*route)(uint32_t intid, uint64_t mpidr);
    void     (*send_sgi)(uint32_t sgi, uint64_t mpidr);
    uint64_t (*cpu_token)(uint64_t mpidr);
    uint32_t (*get_pmr)(void);
    void     (*set_pmr)(uint32_t pmr);
};

// 0 and *ops set, 1 if the device tree has no such controller, -1 if it
// has one that did not set up (printed).
int      gicv2_probe(const struct fdt* f, const struct gic_ops** ops);
int      bcm2836_intc_probe(const struct fdt* f, const struct gic_ops** ops);

#endif
