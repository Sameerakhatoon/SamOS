#ifndef RAMDISK_H
#define RAMDISK_H

#include <stdint.h>

struct disk_driver;

// A disk image the boot loader left in RAM (aarch64: QEMU's
// -initrd, from the device tree's /chosen). The arch hands it over
// before disk_search_and_init; without one the driver mounts nothing.
void ramdisk_attach(uint64_t phys, uint64_t size);
struct disk_driver* ramdisk_driver_init(void);

#endif
