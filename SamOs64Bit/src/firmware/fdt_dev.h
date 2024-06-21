#ifndef FIRMWARE_FDT_DEV_H
#define FIRMWARE_FDT_DEV_H

// The board devices a device tree describes, as plain data:
// the console UART, the harts the kernel may start, DMA coherence and
// the cache controller, a framebuffer the boot loader left, xHCI hosts
// and DesignWare MMC controllers. Written for the JH7110 (VisionFive 2,
// Milk-V Mars) and QEMU's virt and sifive_u, which use the same
// bindings. Like fdt.c: no arch code, no libc, so tests64/host checks
// it against a JH7110 tree.

#include <stdbool.h>
#include <stdint.h>
#include "firmware/fdt.h"

// status "okay" / "ok" or absent.
bool        fdt_node_okay(const struct fdt* f, int node);
// Index of `name` in the string list property `prop` ("reg-names",
// "interrupt-names"), or FDT_ERR_NOTFOUND.
int         fdt_stringlist_index(const struct fdt* f, int node, const char* prop, const char* name);
// Cell `index` of `interrupts` (one-cell specifiers: the PLIC's), 0
// if absent.
uint32_t    fdt_interrupt(const struct fdt* f, int node, int index);

// The console: /chosen stdout-path when it names a 16550 ("ns16550a",
// "ns16550", "snps,dw-apb-uart"), else the first enabled one. The
// DesignWare UART on the JH7110 has 32-bit registers 4 bytes apart
// (reg-io-width 4, reg-shift 2); QEMU's are bytes.
struct fdt_uart {
    int      node;
    uint64_t base, size;
    uint32_t reg_shift, io_width;
};
int         fdt_find_uart(const struct fdt* f, struct fdt_uart* out);

// Why a /cpus child cannot be started as an S-mode hart, NULL if it
// can: disabled, no MMU (the JH7110's S7 monitor core, hart 0, an
// M-mode-only core: no mmu-type, or "riscv,none"), or no A extension
// (riscv,isa, else riscv,isa-extensions).
const char* fdt_hart_unfit(const struct fdt* f, int node);
// The hart has extension `ext` ("a", "zicbom"): a letter of the base
// string, or a "_"-separated name in riscv,isa, or an entry of
// riscv,isa-extensions.
bool        fdt_hart_has_ext(const struct fdt* f, int node, const char* ext);

// Some node (the root, a bus) says dma-noncoherent: devices there do
// not snoop the CPU caches.
bool        fdt_dma_noncoherent(const struct fdt* f);
// A SiFive composable cache (JH7110 "sifive,ccache0", FU540/FU740):
// its registers and line size. 0 or FDT_ERR_NOTFOUND.
int         fdt_find_ccache(const struct fdt* f, struct fdt_region* reg, uint32_t* line);

// A "simple-framebuffer" the boot loader left on (/chosen or anywhere
// in the tree). bpp 0: a format the kernel does not draw.
struct fdt_simplefb {
    int         node;
    uint64_t    base, size;
    uint32_t    width, height, stride, bpp;
    const char* format;
};
int         fdt_find_simplefb(const struct fdt* f, struct fdt_simplefb* out);

// xHCI hosts without PCI: "generic-xhci" / "xhci-platform" (reg 0,
// interrupt 0), and a Cadence USB3 DRD core ("cdns,usb3") in host
// mode (reg "xhci", interrupt "host"). `skipped` gets a reason for the
// first cdns,usb3 that is not a host (dr_mode "peripheral" or "otg").
struct fdt_xhci {
    int      node;
    uint64_t base, size;
    uint32_t irq;
};
int         fdt_find_xhci(const struct fdt* f, struct fdt_xhci* out, int max, const char** skipped);

// DesignWare MMC controllers ("snps,dw-mshc", "starfive,jh7110-mmc").
struct fdt_mmc {
    int      node;
    uint64_t base, size;
    uint32_t irq;
    uint32_t fifo_depth;      // 0: read it from FIFOTH
    uint32_t bus_width;       // 1, 4 or 8
    uint32_t ciu_hz;          // clock-frequency, 0 if not given
    uint32_t max_hz;          // max-frequency, 0 if not given
    bool     non_removable;   // eMMC
    bool     no_sd, no_mmc;
};
int         fdt_find_dw_mmc(const struct fdt* f, struct fdt_mmc* out, int max);

#endif
