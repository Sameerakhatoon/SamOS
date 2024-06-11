#ifndef KERNEL_DISK_DRIVERS_SDHCI_H
#define KERNEL_DISK_DRIVERS_SDHCI_H
// SD Host Controller Interface (SD Host Controller Simplified
// Specification 3.00): the SD card slot of the Raspberry Pi 4 (EMMC2)
// and Pi 3 (Arasan), QEMU's raspi3b and sdhci-pci. See sdhci.c.

#include <stdint.h>

struct disk_driver;

// A controller the platform found (aarch64: the device tree), its
// registers mapped as device memory at `regs`. `base_clock_hz` is used
// when the capabilities register gives no base clock (0: none known).
// `name` is printed. 0, or -1 when the table is full.
int sdhci_add(void* regs, uint64_t phys, uint32_t base_clock_hz, const char* name);

struct disk_driver* sdhci_driver_init(void);

#endif
