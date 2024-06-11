// Raspberry Pi devices: the VideoCore mailbox (property channel
// 8: the framebuffer, clock rates, board revision), the SD card
// controllers, the PM watchdog. See rpi.h.
//
// The mailbox ("brcm,bcm2835-mbox"): a property buffer in RAM, 16-byte
// aligned, whose bus address (the parent bus's dma-ranges: 0xc0000000
// + RAM on the Pi 3 and 4) is written with the channel to the write
// register; the reply comes back the same way. The VideoCore reads and
// writes the buffer behind the CPU's caches: it is cleaned before and
// invalidated after (QEMU has no caches; a board does). The
// framebuffer address it returns is a bus address too.
#include "arch/aarch64/rpi.h"
#include "arch/aarch64/console.h"
#include "arch/aarch64/memmap.h"
#include "arch/aarch64/mmu.h"
#include "arch/aarch64/sysreg.h"
#include "arch/arch.h"
#include "boot/boot_info.h"
#include "disk/drivers/sdhci.h"
#include "string/string.h"

#define MBOX_READ           0x00
#define MBOX_STATUS         0x18
#define MBOX_WRITE          0x20
#define MBOX_FULL           (1u << 31)
#define MBOX_EMPTY          (1u << 30)
#define MBOX_CH_PROP        8
#define MBOX_TIMEOUT_US     1000000

#define PROP_REQUEST        0
#define PROP_OK             0x80000000u
#define TAG_FIRMWARE_REV    0x00000001
#define TAG_BOARD_REV       0x00010002
#define TAG_GET_CLOCK_RATE  0x00030002
#define TAG_ALLOCATE_FB     0x00040001
#define TAG_GET_PITCH       0x00040008
#define TAG_SET_PHYS_WH     0x00048003
#define TAG_SET_VIRT_WH     0x00048004
#define TAG_SET_DEPTH       0x00048005
#define TAG_SET_PIXEL_ORDER 0x00048006
#define TAG_SET_VIRT_OFFSET 0x00048009
#define CLOCK_EMMC          1           // the Arasan SDHCI (Pi 3)
#define CLOCK_EMMC2         12          // the Pi 4's EMMC2

#define PM_RSTC             0x1c
#define PM_WDOG             0x24
#define PM_PASSWORD         0x5a000000u
#define PM_RSTC_FULL_RESET  0x20u

static volatile uint32_t* mbox;
static uint64_t dma_child, dma_parent, dma_size;    // the mailbox bus's dma-ranges
static uint32_t prop[64] __attribute__((aligned(64)));
static int probed;

static uint64_t cells(const uint8_t* p, uint32_t n)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < n * 4; i++)
        v = v << 8 | p[i];
    return v;
}

static int node_ok(const struct fdt* f, int node)
{
    const char* s = fdt_getprop_string(f, node, "status");
    return !s || !strncmp(s, "okay", 5) || !strncmp(s, "ok", 3);
}

static void dcache_civac(const void* p, uint64_t size)
{
    uint64_t line = 4ul << ((sysreg_read("ctr_el0") >> 16) & 0xf);
    for (uint64_t a = (uint64_t)p & ~(line - 1); a < (uint64_t)p + size; a += line)
        __asm__ volatile("dc civac, %0" :: "r"(a) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
}

static uint64_t to_bus(uint64_t pa)
{
    return dma_size && pa >= dma_parent && pa - dma_parent < dma_size ? pa - dma_parent + dma_child : pa;
}

// A bus address the VideoCore returns, as a CPU physical address; Linux
// masks such an address with 0x3fffffff when it is outside dma-ranges.
static uint64_t from_bus(uint64_t bus)
{
    if (dma_size && bus >= dma_child && bus - dma_child < dma_size)
        return bus - dma_child + dma_parent;
    return bus & 0x3fffffff;
}

static int mbox_probe(const struct fdt* f)
{
    if (probed)
        return mbox ? 0 : -1;
    probed = 1;
    int node = fdt_find_compatible(f, -1, "brcm,bcm2835-mbox");
    struct fdt_region reg;
    if (node < 0 || fdt_reg_cpu(f, node, 0, &reg) < 0)
        return -1;
    int bus = fdt_parent(f, node);
    uint32_t len;
    const uint8_t* dr = bus >= 0 ? fdt_getprop(f, bus, "dma-ranges", &len) : 0;
    uint32_t ca = fdt_getprop_u32(f, bus, "#address-cells", 2), sc = fdt_getprop_u32(f, bus, "#size-cells", 1);
    uint32_t pa = fdt_address_cells(f, bus);
    if (dr && len >= (ca + pa + sc) * 4) {
        dma_child = cells(dr, ca);
        dma_parent = cells(dr + ca * 4, pa);
        dma_size = cells(dr + (ca + pa) * 4, sc);
    }
    mbox = paging_map_mmio(reg.base, reg.size, PAGING_CACHE_DISABLED);
    if (mbox)
        console_printf("mailbox: at 0x%lx, bus addresses 0x%lx = RAM 0x%lx (0x%lx bytes)\n", reg.base, dma_child,
                       dma_parent, dma_size);
    return mbox ? 0 : -1;
}

// Send prop[] (its header filled in here) and wait for the answer.
static int mbox_call(uint32_t words)
{
    prop[0] = words * 4;
    prop[1] = PROP_REQUEST;
    uint32_t bus = (uint32_t)to_bus(kimage_pa(prop));
    dcache_civac(prop, sizeof prop);
    uint64_t end = arch_time_us() + MBOX_TIMEOUT_US;
    while (mbox[MBOX_STATUS / 4] & MBOX_FULL)
        if (arch_time_us() > end)
            return -1;
    mbox[MBOX_WRITE / 4] = (bus & ~0xfu) | MBOX_CH_PROP;
    for (;;) {
        while (mbox[MBOX_STATUS / 4] & MBOX_EMPTY)
            if (arch_time_us() > end)
                return -1;
        uint32_t v = mbox[MBOX_READ / 4];
        if ((v & 0xf) == MBOX_CH_PROP && (v & ~0xfu) == (bus & ~0xfu))
            break;
    }
    dcache_civac(prop, sizeof prop);
    return prop[1] == PROP_OK ? 0 : -1;
}

// One tag with `n` value words (in and out) at prop[2]; the values
// follow at prop[5].
static int prop_tag(uint32_t tag, uint32_t n, const uint32_t* in)
{
    prop[2] = tag;
    prop[3] = n * 4;
    prop[4] = 0;
    for (uint32_t i = 0; i < n; i++)
        prop[5 + i] = in ? in[i] : 0;
    prop[5 + n] = 0;        // end tag
    return mbox_call(6 + n) == 0 && (prop[4] & PROP_OK) ? 0 : -1;
}

static uint32_t clock_rate(uint32_t id)
{
    uint32_t in[2] = { id, 0 };
    return prop_tag(TAG_GET_CLOCK_RATE, 2, in) == 0 ? prop[6] : 0;
}

void rpi_sd_init(const struct fdt* f)
{
    // The Pi 5's SD slot (bcm2712, sdhci-brcmstb's standard SDHCI
    // registers) is not run on a board yet; its clock comes from the
    // capabilities register only.
    static const struct { const char* compat; uint32_t clock; } kinds[] = {
        { "brcm,bcm2711-emmc2", CLOCK_EMMC2 },
        { "brcm,bcm2712-sdhci", 0 },
        { "brcm,bcm2835-sdhci", CLOCK_EMMC },
    };
    if (mbox_probe(f) == 0) {
        uint32_t rev[2] = { 0 };
        if (prop_tag(TAG_BOARD_REV, 1, 0) == 0)
            rev[0] = prop[5];
        if (prop_tag(TAG_FIRMWARE_REV, 1, 0) == 0)
            rev[1] = prop[5];
        console_printf("rpi: board revision 0x%x, firmware revision 0x%x\n", rev[0], rev[1]);
    }
    for (unsigned k = 0; k < sizeof kinds / sizeof kinds[0]; k++) {
        int node = fdt_find_compatible(f, -1, kinds[k].compat);
        struct fdt_region reg;
        if (node < 0 || !node_ok(f, node) || fdt_reg_cpu(f, node, 0, &reg) < 0)
            continue;
        void* regs = paging_map_mmio(reg.base, reg.size, PAGING_CACHE_DISABLED);
        uint32_t hz = mbox && kinds[k].clock ? clock_rate(kinds[k].clock) : 0;
        if (regs && sdhci_add(regs, reg.base, hz, kinds[k].compat) == 0)
            console_printf("sdhci: %s at 0x%lx, firmware clock %u Hz\n", kinds[k].compat, reg.base, hz);
        return;     // the first one is the SD slot (the Pi 4's Arasan serves the Wi-Fi)
    }
}

int rpi_framebuffer(const struct fdt* f, uint32_t width, uint32_t height, struct rpi_fb* fb)
{
    if (mbox_probe(f) < 0)
        return -1;
    uint32_t* t = &prop[2];
    uint32_t n = 0;
#define TAG(id, words, ...) do { uint32_t v_[] = { __VA_ARGS__ }; t[n++] = id; t[n++] = (words) * 4; t[n++] = 0; \
        for (uint32_t i_ = 0; i_ < (words); i_++) t[n++] = i_ < sizeof v_ / 4 ? v_[i_] : 0; } while (0)
    TAG(TAG_SET_PHYS_WH, 2, width, height);
    TAG(TAG_SET_VIRT_WH, 2, width, height);
    TAG(TAG_SET_VIRT_OFFSET, 2, 0, 0);
    uint32_t depth = n + 3;
    TAG(TAG_SET_DEPTH, 1, 32);
    uint32_t order = n + 3;
    TAG(TAG_SET_PIXEL_ORDER, 1, 0);         // 0: BGR, the graphics code's order
    uint32_t alloc = n + 3;
    TAG(TAG_ALLOCATE_FB, 2, 4096, 0);
    uint32_t pitch = n + 3;
    TAG(TAG_GET_PITCH, 1, 0);
#undef TAG
    t[n++] = 0;
    if (mbox_call(n + 2) < 0 || !t[alloc] || t[depth] != 32) {
        console_printf("mailbox: framebuffer %ux%ux32 refused\n", width, height);
        return -1;
    }
    fb->pa = from_bus(t[alloc]);
    fb->size = t[alloc + 1];
    fb->width = t[3];               // TAG_SET_PHYS_WH's answer
    fb->height = t[4];
    fb->pitch = t[pitch];
    fb->format = t[order] ? SAMOS_FB_FORMAT_RGBX : SAMOS_FB_FORMAT_BGRX;
    if (!fb->width || !fb->height || fb->pitch < fb->width * 4 || fb->size < (uint64_t)fb->pitch * fb->height) {
        console_printf("mailbox: framebuffer answer does not add up (%ux%u, pitch %u, %lu bytes)\n", fb->width,
                       fb->height, fb->pitch, fb->size);
        return -1;
    }
    console_printf("mailbox: framebuffer %ux%u, 32 bpp, %s, pitch %u, %lu bytes at bus 0x%x = pa 0x%lx\n",
                   fb->width, fb->height, t[order] ? "RGB" : "BGR", fb->pitch, fb->size, t[alloc], fb->pa);
    return 0;
}

void rpi_reset(const struct fdt* f)
{
    static const char* const compat[] = { "brcm,bcm2835-pm-wdt", "brcm,bcm2835-pm", "brcm,bcm2711-pm" };
    struct fdt_region reg;
    int node = -1;
    for (unsigned i = 0; i < 3 && node < 0; i++)
        node = fdt_find_compatible(f, -1, compat[i]);
    if (node < 0 || fdt_reg_cpu(f, node, 0, &reg) < 0)
        return;
    volatile uint32_t* pm = paging_map_mmio(reg.base, reg.size, PAGING_CACHE_DISABLED);
    if (!pm)
        return;
    console_printf("rpi: restart by the PM watchdog at 0x%lx\n", reg.base);
    pm[PM_WDOG / 4] = PM_PASSWORD | 10;
    pm[PM_RSTC / 4] = PM_PASSWORD | (pm[PM_RSTC / 4] & ~0x30u) | PM_RSTC_FULL_RESET;
    uint64_t end = arch_time_us() + 1000000;
    while (arch_time_us() < end)
        ;
}
