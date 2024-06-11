// The Raspberry Pi 2/3 interrupt controllers, for QEMU's raspi3b
// (no GIC there): the BCM2836 per-core controller ("brcm,bcm2836-l1-
// intc": each core's generic timer lines and four mailboxes) and the
// BCM2835 ARM controller behind it ("brcm,bcm2836-armctrl-ic": the GPU
// peripherals' lines, which reach one core as the local "GPU" source).
// gicv3.h's calls reach it through struct gic_ops. A Pi 4 has the same
// per-core block, but its firmware routes everything through the
// GIC-400 (enable_gic=1), which gic_init prefers.
//
// It keeps the GIC's INTID numbering, so the rest of the kernel does
// not change:
//   SGI 0-15    bit n of the core's mailbox 0 (set by another core:
//               the IPI); cleared when acknowledged
//   PPI 16+n    local source n: 0-3 the timers (17 = CNTPNSIRQ, the EL1
//               physical timer), 9 the PMU
//   SPI 32+32*b+n  bank b of the ARM controller (0 basic, 1 and 2 GPU
//               lines 0-31 and 32-63), line n: the device tree's two
//               cells <b n>
// No priorities and no priority mask: set_priority, set_edge and the
// mask are no-ops, and a disk wait takes every interrupt (no disk here
// completes by interrupt: the SD card is polled). Every line is level
// (the device keeps it up until its handler quiets it) except the
// mailbox bits, which the acknowledge clears.
#include "arch/aarch64/gicv3.h"
#include "arch/aarch64/console.h"
#include "arch/aarch64/mmu.h"
#include "arch/aarch64/sysreg.h"

#define LOCAL_GPU_ROUTING   0x0c
#define LOCAL_TIMER_CTL(c)  (0x40 + 4 * (c))
#define LOCAL_MBOX_CTL(c)   (0x50 + 4 * (c))
#define LOCAL_IRQ_SRC(c)    (0x60 + 4 * (c))
#define LOCAL_MBOX0_SET(c)  (0x80 + 16 * (c))
#define LOCAL_MBOX0_CLR(c)  (0xc0 + 16 * (c))
#define SRC_TIMERS          0x00f
#define SRC_MBOX0           0x010
#define SRC_GPU             0x100

// ARM controller: pending, enable and disable registers per bank.
static const uint16_t pending_reg[3] = { 0x00, 0x04, 0x08 };
static const uint16_t enable_reg[3]  = { 0x18, 0x10, 0x14 };
static const uint16_t disable_reg[3] = { 0x24, 0x1c, 0x20 };

static uint64_t local, armctrl;
static uint32_t enabled[3];         // the lines enabled per bank (pending shows them all)
static int local_node, armctrl_node;

static inline uint32_t rd32(uint64_t a) { return *(volatile uint32_t*)a; }
static inline void wr32(uint64_t a, uint32_t v) { *(volatile uint32_t*)a = v; }

static uint32_t core(void)
{
    return sysreg_read("mpidr_el1") & 3;
}

static int bcm_init_cpu(void)
{
    uint32_t c = core();
    wr32(local + LOCAL_TIMER_CTL(c), 0);
    wr32(local + LOCAL_MBOX_CTL(c), 0);
    wr32(local + LOCAL_MBOX0_CLR(c), ~0u);
    return 0;
}

static void bcm_enable(uint32_t intid)
{
    uint32_t c = core();
    if (intid < GIC_INTID_PPI) {
        wr32(local + LOCAL_MBOX_CTL(c), rd32(local + LOCAL_MBOX_CTL(c)) | 1);
    } else if (intid < GIC_INTID_PPI + 4) {
        wr32(local + LOCAL_TIMER_CTL(c), rd32(local + LOCAL_TIMER_CTL(c)) | 1u << (intid - GIC_INTID_PPI));
    } else if (intid >= GIC_INTID_SPI && intid < GIC_INTID_SPI + 96 && armctrl) {
        uint32_t b = (intid - GIC_INTID_SPI) / 32, bit = 1u << (intid % 32);
        enabled[b] |= bit;
        wr32(armctrl + enable_reg[b], bit);
    }
}

static void bcm_disable(uint32_t intid)
{
    uint32_t c = core();
    if (intid < GIC_INTID_PPI) {
        // The mailbox enable covers all its bits: an SGI stays on.
    } else if (intid < GIC_INTID_PPI + 4) {
        wr32(local + LOCAL_TIMER_CTL(c), rd32(local + LOCAL_TIMER_CTL(c)) & ~(1u << (intid - GIC_INTID_PPI)));
    } else if (intid >= GIC_INTID_SPI && intid < GIC_INTID_SPI + 96 && armctrl) {
        uint32_t b = (intid - GIC_INTID_SPI) / 32, bit = 1u << (intid % 32);
        enabled[b] &= ~bit;
        wr32(armctrl + disable_reg[b], bit);
    }
}

static uint32_t bcm_ack(void)
{
    uint32_t c = core();
    uint32_t src = rd32(local + LOCAL_IRQ_SRC(c));
    if (src & SRC_MBOX0) {
        uint32_t m = rd32(local + LOCAL_MBOX0_CLR(c));
        if (m) {
            uint32_t sgi = (uint32_t)__builtin_ctz(m);
            wr32(local + LOCAL_MBOX0_CLR(c), 1u << sgi);
            return sgi & 0xf;
        }
    }
    if (src & SRC_TIMERS)
        return GIC_INTID_PPI + (uint32_t)__builtin_ctz(src & SRC_TIMERS);
    if ((src & SRC_GPU) && armctrl) {
        for (uint32_t b = 0; b < 3; b++) {
            uint32_t p = rd32(armctrl + pending_reg[b]) & enabled[b];
            if (p)
                return GIC_INTID_SPI + 32 * b + (uint32_t)__builtin_ctz(p);
        }
    }
    return 1023;
}

static void bcm_eoi(uint32_t intid)
{
}

static void bcm_nop_u8(uint32_t intid, uint8_t v)
{
}

static void bcm_nop_int(uint32_t intid, int v)
{
}

// Every GPU line goes to one core: the one asked for last.
static void bcm_route(uint32_t intid, uint64_t mpidr)
{
    if (intid >= GIC_INTID_SPI)
        wr32(local + LOCAL_GPU_ROUTING, (uint32_t)(mpidr & 3));
}

static void bcm_send_sgi(uint32_t sgi, uint64_t mpidr)
{
    __asm__ volatile("dsb ishst" ::: "memory");
    wr32(local + LOCAL_MBOX0_SET(mpidr & 3), 1u << (sgi & 0xf));
}

// Cores 0-3 only (Aff0, as on the BCM2836/7).
static uint64_t bcm_cpu_token(uint64_t mpidr)
{
    return (mpidr & 0xff00fffffcull) ? 0 : local;
}

static uint32_t bcm_get_pmr(void)
{
    return 0xff;
}

static void bcm_set_pmr(uint32_t pmr)
{
}

static uint32_t be32(const uint8_t* p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

// The node whose phandle `node` (or its nearest ancestor) names in
// interrupt-parent; -1 if none.
static int interrupt_parent(const struct fdt* f, int node)
{
    uint32_t ph = 0;
    for (int n = node; n >= 0 && !ph; n = n ? fdt_parent(f, n) : -1)
        ph = fdt_getprop_u32(f, n, "interrupt-parent", 0);
    if (!ph)
        return -1;
    for (int n = fdt_next_node(f, -1, 0); n >= 0; n = fdt_next_node(f, n, 0))
        if (fdt_getprop_u32(f, n, "phandle", 0) == ph || fdt_getprop_u32(f, n, "linux,phandle", 0) == ph)
            return n;
    return -1;
}

// The two cells of `node`'s interrupt `index` and the controller they
// belong to.
static int dt_cells(const struct fdt* f, int node, int index, uint32_t* a, uint32_t* b)
{
    uint32_t len;
    const uint8_t* p = fdt_getprop(f, node, "interrupts", &len);
    if (!p || (uint32_t)(index + 1) * 8 > len)
        return -1;
    *a = be32(p + index * 8);
    *b = be32(p + index * 8 + 4);
    return interrupt_parent(f, node);
}

static int bcm_dt_intid(const struct fdt* f, int node, int index)
{
    uint32_t a, b;
    int parent = dt_cells(f, node, index, &a, &b);
    if (parent >= 0 && parent == local_node && a < 16)
        return (int)(GIC_INTID_PPI + a);
    if (parent >= 0 && parent == armctrl_node && a < 3 && b < 32)
        return (int)(GIC_INTID_SPI + 32 * a + b);
    return -1;
}

static uint32_t bcm_dt_flags(const struct fdt* f, int node, int index)
{
    uint32_t a, b;
    int parent = dt_cells(f, node, index, &a, &b);
    return parent >= 0 && parent == local_node ? b : 4;     // the ARM controller's lines are level
}

static const struct gic_ops bcm2836_ops = {
    .name = "BCM2836",
    .init_cpu = bcm_init_cpu,
    .enable = bcm_enable,
    .disable = bcm_disable,
    .ack = bcm_ack,
    .eoi = bcm_eoi,
    .dt_intid = bcm_dt_intid,
    .dt_flags = bcm_dt_flags,
    .set_priority = bcm_nop_u8,
    .set_edge = bcm_nop_int,
    .route = bcm_route,
    .send_sgi = bcm_send_sgi,
    .cpu_token = bcm_cpu_token,
    .get_pmr = bcm_get_pmr,
    .set_pmr = bcm_set_pmr,
};

int bcm2836_intc_probe(const struct fdt* f, const struct gic_ops** ops)
{
    struct fdt_region reg;
    local_node = fdt_find_compatible(f, -1, "brcm,bcm2836-l1-intc");
    if (local_node < 0)
        return 1;
    if (fdt_reg_cpu(f, local_node, 0, &reg) < 0 || !(local = mmu_map_device(reg.base, reg.size))) {
        console_printf("gic: cannot map the BCM2836 local interrupt controller\n");
        return -1;
    }
    uint64_t local_pa = reg.base;
    armctrl_node = fdt_find_compatible(f, -1, "brcm,bcm2836-armctrl-ic");
    if (armctrl_node < 0)
        armctrl_node = fdt_find_compatible(f, -1, "brcm,bcm2835-armctrl-ic");
    if (armctrl_node >= 0 && fdt_reg_cpu(f, armctrl_node, 0, &reg) == 0)
        armctrl = mmu_map_device(reg.base, reg.size);
    if (armctrl)
        for (int b = 0; b < 3; b++)
            wr32(armctrl + disable_reg[b], ~0u);
    wr32(local + LOCAL_GPU_ROUTING, core());
    bcm_init_cpu();
    *ops = &bcm2836_ops;
    console_printf("gic: none; BCM2836 per-core controller at 0x%lx, ARM controller %s0x%lx\n", local_pa,
                   armctrl ? "at " : "missing ", armctrl ? reg.base : 0);
    return 0;
}
