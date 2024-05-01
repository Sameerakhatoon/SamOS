#ifndef FIRMWARE_RAMFB_H
#define FIRMWARE_RAMFB_H
// QEMU's ramfb (-device ramfb): a linear framebuffer in guest RAM
// that the guest describes through fw_cfg, the screen of the ports'
// QEMU virt machines (they have no VGA). Arch-neutral: the caller
// finds fw_cfg (device tree "qemu,fw-cfg-mmio"), maps its registers
// uncached and gives a DMA buffer it knows the physical address of.

#include <stdint.h>

struct ramfb_dma {
    struct {
        uint32_t control, length;
        uint64_t address;
    } __attribute__((packed)) access;          // FWCfgDmaAccess
    struct {
        uint64_t addr;
        uint32_t fourcc, flags, width, height, stride;
    } __attribute__((packed)) cfg;             // RAMFBCfg
} __attribute__((aligned(16)));

// Show the XRGB8888 (SAMOS_FB_FORMAT_BGRX) framebuffer at `fb_pa`,
// width x height, pitch width. 0, or < 0: -1 not fw_cfg, -2 no DMA
// interface, -3 no ramfb device, -4 the device refused, -5 no answer.
int ramfb_setup(volatile void* fw_cfg, struct ramfb_dma* dma, uint64_t dma_pa, uint64_t fb_pa, uint32_t width,
                uint32_t height);

#endif
