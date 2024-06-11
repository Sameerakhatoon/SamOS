#ifndef ARCH_AARCH64_RPI_H
#define ARCH_AARCH64_RPI_H

#include <stdint.h>
#include "firmware/fdt.h"

// Raspberry Pi devices found through the device tree the
// firmware hands over (QEMU raspi3b: tests64/arch/dtb-raspi3b.py's).
// Each is a no-op on a tree without them (QEMU virt).

// The SD card controllers (Pi 4 EMMC2, else the Pi 3's Arasan SDHCI)
// handed to disk/drivers/sdhci.c. The base clock, when the controller
// does not report one, is the firmware's (mailbox) rate for it.
void rpi_sd_init(const struct fdt* f);

// A 32-bit framebuffer from the VideoCore over the mailbox's property
// channel: 0 and the fields set, or -1 (no mailbox, or refused).
struct rpi_fb {
    uint64_t pa;            // CPU physical
    uint64_t size;
    uint32_t width, height;
    uint32_t pitch;         // bytes
    uint32_t format;        // SAMOS_FB_FORMAT_BGRX or _RGBX
};
int  rpi_framebuffer(const struct fdt* f, uint32_t width, uint32_t height, struct rpi_fb* fb);

// Restart through the PM block's watchdog (no PSCI on a Pi 3 or under
// the Pi 4's armstub); returns if there is none.
void rpi_reset(const struct fdt* f);

#endif
