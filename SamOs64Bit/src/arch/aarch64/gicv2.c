// GICv2 (ARM IHI 0048B): the GIC-400 of the Pi 4 and Pi 5, and
// QEMU virt with gic-version=2. gicv3.h's calls reach it through struct
// gic_ops (gic_init picks it when the device tree has no GICv3).
//
// Differences from the GICv3 path: the CPU interface is memory mapped
// (GICC, the device tree's second reg entry), the distributor's SGI and
// PPI registers are banked per CPU, an SPI targets CPU interfaces by a
// bit mask (GICD_ITARGETSR) and an SGI by a target list (GICD_SGIR),
// both in CPU interface numbers, which are not MPIDRs: each CPU reads
// its own from the banked GICD_ITARGETSR0 (on a Cortex-A76, Pi 5, the
// core number is Aff1, not Aff0). The EOI of an SGI carries the source
// CPU its acknowledge returned.
//
// Groups: the kernel keeps the group the firmware left and enables bit
// 0 of GICD_CTLR and GICC_CTLR. Without security extensions (QEMU)
// everything is Group 0, signalled as IRQ (FIQEn clear); behind the
// Pi's armstub the kernel runs non-secure, the armstub made every
// interrupt Group 1, and bit 0 is the non-secure Group 1 enable. A
// non-secure priority write is halved and offset by 0x80, for the lines
// and the mask alike, so PRIO_COMPLETION < PMR_DISK_WAIT < 0xa0 (arch.c)
// keeps its order. No Group 0 for the kernel there, so no FIQ panic
// stop on GICv2 (gic_fiq_sgi is 0, as with TF-A on GICv3).
#include "arch/aarch64/gicv3.h"
#include "arch/aarch64/console.h"
#include "arch/aarch64/mmu.h"
#include "arch/aarch64/sysreg.h"
#include "cpu/percpu.h"

#define GICD_CTLR           0x000
#define GICD_TYPER          0x004
#define GICD_ISENABLER      0x100
#define GICD_ICENABLER      0x180
#define GICD_ICPENDR        0x280
#define GICD_IPRIORITYR     0x400
#define GICD_ITARGETSR      0x800
#define GICD_ICFGR          0xc00
#define GICD_SGIR           0xf00

#define GICC_CTLR           0x00
#define GICC_PMR            0x04
#define GICC_BPR            0x08
#define GICC_IAR            0x0c
#define GICC_EOIR           0x10

#define PRIORITY_DEFAULT    0xa0a0a0a0u
#define MAX_IFACES          8

static uint64_t gicd, gicc;
static uint32_t gic_lines;
static uint64_t iface_mpidr[MAX_IFACES];    // MPIDR of CPU interface n
static uint8_t iface_seen;                  // bit n: interface n is set up
static uint32_t sgi_iar[SAMOS_MAX_CPUS][16]; // the IAR (with its source CPU) of each SGI taken

static inline uint32_t rd32(uint64_t a) { return *(volatile uint32_t*)a; }
static inline void wr32(uint64_t a, uint32_t v) { *(volatile uint32_t*)a = v; }
static inline void wr8(uint64_t a, uint8_t v) { *(volatile uint8_t*)a = v; }

static uint64_t my_mpidr(void)
{
    return sysreg_read("mpidr_el1") & 0xff00ffffffull;
}

// The bit mask of the CPU interface whose MPIDR this is; 0 if that CPU
// has not set up its interface yet.
static uint8_t iface_mask(uint64_t mpidr)
{
    for (int i = 0; i < MAX_IFACES; i++)
        if ((iface_seen >> i & 1) && iface_mpidr[i] == (mpidr & 0xff00ffffffull))
            return (uint8_t)(1u << i);
    return 0;
}

static unsigned cpu_slot(void)
{
    struct cpu* c = arch_cpu_self();
    return c ? c->index : 0;
}

// This CPU's banked SGI/PPI registers and its CPU interface.
static int v2_init_cpu(void)
{
    wr32(gicd + GICD_ICENABLER, ~0u);
    wr32(gicd + GICD_ICPENDR, ~0u);
    for (uint32_t i = 0; i < 32; i += 4)
        wr32(gicd + GICD_IPRIORITYR + i, PRIORITY_DEFAULT);
    // Banked: reads as this CPU's own interface bit (0 on a GIC built
    // for one CPU, which is interface 0).
    uint8_t mask = (uint8_t)rd32(gicd + GICD_ITARGETSR);
    int n = mask ? __builtin_ctz(mask) : 0;
    iface_mpidr[n] = my_mpidr();
    __atomic_or_fetch(&iface_seen, (uint8_t)(1u << n), __ATOMIC_SEQ_CST);
    wr32(gicc + GICC_PMR, 0xff);
    wr32(gicc + GICC_BPR, 0);
    wr32(gicc + GICC_CTLR, 1);
    return 0;
}

static void v2_enable(uint32_t intid)
{
    if (intid < gic_lines)
        wr32(gicd + GICD_ISENABLER + intid / 32 * 4, 1u << (intid % 32));
}

static void v2_disable(uint32_t intid)
{
    if (intid < gic_lines)
        wr32(gicd + GICD_ICENABLER + intid / 32 * 4, 1u << (intid % 32));
}

static uint32_t v2_ack(void)
{
    uint32_t iar = rd32(gicc + GICC_IAR);
    uint32_t intid = iar & 0x3ff;
    if (intid < 16)
        sgi_iar[cpu_slot()][intid] = iar;
    return intid;
}

static void v2_eoi(uint32_t intid)
{
    wr32(gicc + GICC_EOIR, intid < 16 ? sgi_iar[cpu_slot()][intid] : intid);
}

static void v2_set_priority(uint32_t intid, uint8_t priority)
{
    if (intid < gic_lines)
        wr8(gicd + GICD_IPRIORITYR + intid, priority);
}

static void v2_set_edge(uint32_t intid, int edge)
{
    if (intid < GIC_INTID_SPI || intid >= gic_lines)
        return;
    uint64_t reg = gicd + GICD_ICFGR + intid / 16 * 4;
    uint32_t bit = 2u << (2 * (intid % 16));
    wr32(reg, edge ? rd32(reg) | bit : rd32(reg) & ~bit);
}

static void v2_route(uint32_t intid, uint64_t mpidr)
{
    uint8_t mask = iface_mask(mpidr);
    if (intid >= GIC_INTID_SPI && intid < gic_lines && mask)
        wr8(gicd + GICD_ITARGETSR + intid, mask);
}

static void v2_send_sgi(uint32_t sgi, uint64_t mpidr)
{
    uint8_t mask = iface_mask(mpidr);
    if (!mask)
        return;
    __asm__ volatile("dsb ishst" ::: "memory");
    wr32(gicd + GICD_SGIR, (uint32_t)mask << 16 | (sgi & 0xf));
}

// No per-CPU frame to find: any CPU may come (it registers its
// interface number in v2_init_cpu).
static uint64_t v2_cpu_token(uint64_t mpidr)
{
    return gicc;
}

static uint32_t v2_get_pmr(void)
{
    return rd32(gicc + GICC_PMR);
}

static void v2_set_pmr(uint32_t pmr)
{
    wr32(gicc + GICC_PMR, pmr);
}

static const struct gic_ops gicv2_ops = {
    .name = "GICv2",
    .init_cpu = v2_init_cpu,
    .enable = v2_enable,
    .disable = v2_disable,
    .ack = v2_ack,
    .eoi = v2_eoi,
    .set_priority = v2_set_priority,
    .set_edge = v2_set_edge,
    .route = v2_route,
    .send_sgi = v2_send_sgi,
    .cpu_token = v2_cpu_token,
    .get_pmr = v2_get_pmr,
    .set_pmr = v2_set_pmr,
    // dt_intid, dt_flags: 0, the GIC binding as on GICv3
};

static const char* const compatibles[] = {
    "arm,gic-400", "arm,cortex-a15-gic", "arm,cortex-a9-gic", "arm,cortex-a7-gic",
};

int gicv2_probe(const struct fdt* f, const struct gic_ops** ops)
{
    int node = -1;
    for (unsigned i = 0; i < sizeof compatibles / sizeof compatibles[0] && node < 0; i++)
        node = fdt_find_compatible(f, -1, compatibles[i]);
    if (node < 0)
        return 1;
    struct fdt_region d, c;
    if (fdt_reg_cpu(f, node, 0, &d) < 0 || fdt_reg_cpu(f, node, 1, &c) < 0) {
        console_printf("gic: GICv2 node without distributor and CPU interface regs\n");
        return -1;
    }
    gicd = mmu_map_device(d.base, d.size);
    gicc = mmu_map_device(c.base, c.size);
    if (!gicd || !gicc) {
        console_printf("gic: cannot map the GICv2 at 0x%lx, 0x%lx\n", d.base, c.base);
        return -1;
    }
    wr32(gicd + GICD_CTLR, 0);
    uint32_t typer = rd32(gicd + GICD_TYPER);
    gic_lines = 32 * ((typer & 0x1f) + 1);
    if (gic_lines > GIC_INTID_SPURIOUS)
        gic_lines = GIC_INTID_SPURIOUS;
    v2_init_cpu();
    // SPIs: disabled, not pending, default priority, to this CPU.
    uint32_t me = iface_mask(my_mpidr());
    for (uint32_t i = GIC_INTID_SPI; i < gic_lines; i += 32) {
        wr32(gicd + GICD_ICENABLER + i / 8, ~0u);
        wr32(gicd + GICD_ICPENDR + i / 8, ~0u);
    }
    for (uint32_t i = GIC_INTID_SPI; i < gic_lines; i += 4) {
        wr32(gicd + GICD_IPRIORITYR + i, PRIORITY_DEFAULT);
        wr32(gicd + GICD_ITARGETSR + i, me * 0x01010101u);
    }
    wr32(gicd + GICD_CTLR, 1);
    *ops = &gicv2_ops;
    console_printf("gic: GICv2 distributor at 0x%lx, CPU interface at 0x%lx, %u INTIDs, %u CPU interface(s)\n",
                   d.base, c.base, gic_lines, ((typer >> 5) & 7) + 1);
    return 0;
}
