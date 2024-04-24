#ifndef ARCH_AARCH64_ACPI_DT_H
#define ARCH_AARCH64_ACPI_DT_H

#include <stdint.h>
#include "firmware/fdt.h"

// ACPI on Arm, for a UEFI boot. The shared parser (acpi/acpi.c)
// reads the MADT GIC entries, SPCR, GTDT and FADT; this file turns
// them into the few device tree nodes the drivers read (gicv3.c,
// pl011.c, timer.c, arch.c's PSCI), so one set of drivers serves both.
//
// acpi_dt_build: parse the tables at `rsdp` and write a DTB with
//   /chosen (bootargs, stdout-path), /memory (the RAM of memmap.c),
//   /cpus (MADT GICC), /psci (FADT), the arm,gic-v3 node (MADT GICD
//   and GICR), the arm,armv8-timer node (GTDT) and the arm,pl011 node
//   (SPCR). `out` is set up on it. 0, or -1 without a usable MADT.
//   Runs with the MMU on (the tables through the direct map) and
//   prints one "acpi:" line per table it used.
// AAVMF gives either a DTB or ACPI tables, never both (QEMU's
// acpi=off/on), so there is no DT to cross-check against here; the
// arch test compares the two boots instead (aarch64-uefi.sh).

int  acpi_dt_build(uint64_t rsdp, const char* bootargs, struct fdt* out);

#endif
