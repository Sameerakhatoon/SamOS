#ifndef DISK_DRIVERS_DW_MMC_H
#define DISK_DRIVERS_DW_MMC_H
// Synopsys DesignWare Mobile Storage Host (dw_mmc: device tree
// "snps,dw-mshc", the JH7110's "starfive,jh7110-mmc"), an SD card or
// eMMC as a disk. See dw_mmc.c.

#include <stdbool.h>
#include <stdint.h>

#define DW_MMC_BOUNCE_PAGES 16          // 64 KiB, 128 sectors per command

// What the device tree says about one controller (firmware/fdt_dev.h
// fills it on the ports that have one).
struct dw_mmc_params {
    uint64_t phys, size;
    uint32_t fifo_depth;                // 0: from FIFOTH's reset value
    uint32_t bus_width;                 // 1, 4 or 8
    uint32_t ciu_hz;                    // the controller clock, 0 unknown
    uint32_t max_hz;                    // the card clock limit, 0 none
    bool     non_removable;             // eMMC (or a soldered card)
    bool     no_sd, no_mmc;
};

struct dw_mmc_host {
    volatile uint8_t* regs;
    struct dw_mmc_params p;
    uint32_t data_off;                  // the FIFO: 0x100, 0x200 from version 2.40a
    uint32_t data_width;                // FIFO word, 4 or 8 bytes (HCON)
    bool     idmac;                     // the internal DMA is used
    bool     idmac64;                   // with 64-bit descriptors
    const char* pio_why;                // why not, for the log
    // The card.
    bool     sd;                        // else MMC / eMMC
    bool     block_addr;                // SDHC/SDXC, sector-mode eMMC
    uint32_t rca;
    uint32_t csd[4], cid[4];            // csd[0] is bits 31:0
    uint64_t sectors;
    uint32_t clock_hz;                  // the card clock now (assumed ciu / divider)
    uint32_t width;                     // the bus now
    bool     broken;
    // IDMAC: a page of descriptors, the bounce pages.
    void*    desc;
    void*    bounce[DW_MMC_BOUNCE_PAGES];
};

// Reset the controller, find and set up the card: 0, or < 0 with a
// line saying why.
int  dw_mmc_init(struct dw_mmc_host* h);
// `count` 512-byte sectors from `lba`, through the bounce pages
// (IDMAC) or the FIFO (PIO).
int  dw_mmc_io(struct dw_mmc_host* h, uint64_t lba, uint32_t count, void* buf, bool write);

#ifndef DW_MMC_HOST_TEST
struct disk_driver;
// A controller from the device tree (arch_firmware_init); the disk
// driver's mount starts it.
void dw_mmc_attach(const struct dw_mmc_params* p);
struct disk_driver* dw_mmc_driver_init(void);
#endif

#endif
