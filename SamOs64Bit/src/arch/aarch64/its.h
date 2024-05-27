#ifndef ARCH_AARCH64_ITS_H
#define ARCH_AARCH64_ITS_H

#include <stdbool.h>
#include <stdint.h>
#include "firmware/fdt.h"

// S-PAR - the GICv3 Interrupt Translation Service: a PCI function's MSI
// or MSI-X write of EventID to the doorbell (GITS_TRANSLATER) becomes
// LPI `lpi` on a CPU. The PCI side is arch.c's arch_irq_msi; INTIDs
// GIC_INTID_LPI.. (gicv3.h) dispatch like SPIs (trap.c).
//
// its_init: the "arm,gic-v3-its" node under the GIC, after gic_init
// (arch_irq_init); its tables, every redistributor's LPI tables and
// EnableLPIs. 0, or < 0 (printed; then no MSI, drivers use INTx), also
// with -machine virt,its=off.
// its_map: route event `event` of device `devid` to `lpi` at priority
// `prio` on the CPU whose redistributor is `gicr`; once mapped, the
// same call moves it there. 0 or < 0.
int      its_init(const struct fdt* f);
bool     its_ready(void);
uint64_t its_doorbell(void);
int      its_map(uint32_t devid, uint32_t event, uint32_t lpi, uint64_t gicr, uint8_t prio);

#endif
