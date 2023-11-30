// GICv3 distributor, redistributor and CPU interface. See
// gicv3.h. Register names and offsets: ARM IHI 0069 (GICv3/v4).
//
// Works in either security view: QEMU virt without EL3 has a single
// security state (GICD_CTLR.DS = 1), real boards with TF-A give the
// kernel the non-secure view; the bits written below mean the same
// (ARE, then Group 1 enable) in both.
#include "arch/aarch64/gicv3.h"
#include "arch/aarch64/console.h"
#include "arch/aarch64/sysreg.h"

#define GICD_CTLR           0x0000
#define GICD_TYPER          0x0004
#define GICD_IGROUPR        0x0080
#define GICD_ISENABLER      0x0100
#define GICD_ICENABLER      0x0180
#define GICD_ICPENDR        0x0280
#define GICD_IPRIORITYR     0x0400
#define GICD_IROUTER        0x6000      // 64-bit per INTID, SPIs only
#define GICD_CTLR_G1        (1u << 0)   // EnableGrp1 (NS view) / EnableGrp0 (DS)
#define GICD_CTLR_G1A       (1u << 1)   // EnableGrp1A (NS view) / EnableGrp1 (DS)
#define GICD_CTLR_ARE       (1u << 4)   // affinity routing
#define GICD_CTLR_RWP       (1u << 31)

#define GICR_CTLR           0x0000      // RD_base frame
#define GICR_TYPER          0x0008      // 64-bit: [63:32] affinity
#define GICR_WAKER          0x0014
#define GICR_CTLR_RWP       (1u << 3)
#define GICR_TYPER_VLPIS    (1u << 1)   // GICv4: two more 64 KiB frames
#define GICR_TYPER_LAST     (1u << 4)
#define GICR_WAKER_SLEEP    (1u << 1)   // ProcessorSleep
#define GICR_WAKER_ASLEEP   (1u << 2)   // ChildrenAsleep
#define GICR_SGI            0x10000     // SGI_base frame: SGI and PPI registers
#define GICR_IGROUPR0       0x0080
#define GICR_ISENABLER0     0x0100
#define GICR_ICENABLER0     0x0180
#define GICR_ICPENDR0       0x0280
#define GICR_IPRIORITYR     0x0400
#define GICR_IGRPMODR0      0x0d00

#define PRIORITY_DEFAULT    0xa0a0a0a0u
#define MAX_RD_REGIONS      4

static uint64_t gicd;           // distributor
static uint64_t gicr;           // this CPU's redistributor (RD_base)
static uint32_t gic_lines;      // INTIDs the distributor implements
static uint32_t intid_cells;    // #interrupt-cells

static inline uint32_t rd32(uint64_t a) { return *(volatile uint32_t*)a; }
static inline void wr32(uint64_t a, uint32_t v) { *(volatile uint32_t*)a = v; }
static inline void wr64(uint64_t a, uint64_t v) { *(volatile uint64_t*)a = v; }

static void wait_clear(uint64_t reg, uint32_t bit)
{
    while (rd32(reg) & bit)
        ;
}

// MPIDR_EL1 affinity as GICR_TYPER packs it: Aff3.Aff2.Aff1.Aff0.
static uint32_t cpu_affinity(void)
{
    uint64_t m = sysreg_read("mpidr_el1");
    return (uint32_t)(((m >> 8) & 0xff000000) | (m & 0xffffff));
}

// Walk the redistributor frames of every region for this CPU's
// affinity. Sets gicr; returns its index, *count = all frames seen.
static int find_redistributor(const struct fdt* f, int node, uint32_t aff, int* count)
{
    int regions = (int)fdt_getprop_u32(f, node, "#redistributor-regions", 1);
    int found = -1;
    *count = 0;
    for (int r = 0; r < regions && r < MAX_RD_REGIONS; r++) {
        struct fdt_region reg;
        if (fdt_reg_cpu(f, node, 1 + r, &reg) < 0)
            break;
        for (uint64_t rd = reg.base; rd < reg.base + reg.size;) {
            uint32_t lo = rd32(rd + GICR_TYPER), hi = rd32(rd + GICR_TYPER + 4);
            if (hi == aff && found < 0) {
                gicr = rd;
                found = *count;
            }
            (*count)++;
            if (lo & GICR_TYPER_LAST)
                break;
            rd += lo & GICR_TYPER_VLPIS ? 0x40000 : 0x20000;
        }
    }
    return found;
}

static void distributor_init(uint32_t aff)
{
    uint64_t route = ((uint64_t)(aff >> 24) << 32) | (aff & 0xffffff);

    wr32(gicd + GICD_CTLR, 0);
    wait_clear(gicd + GICD_CTLR, GICD_CTLR_RWP);
    gic_lines = 32 * ((rd32(gicd + GICD_TYPER) & 0x1f) + 1);
    if (gic_lines > GIC_INTID_SPURIOUS)
        gic_lines = GIC_INTID_SPURIOUS;
    // SPIs: Group 1, disabled, not pending, default priority, to us.
    for (uint32_t i = GIC_INTID_SPI; i < gic_lines; i += 32) {
        wr32(gicd + GICD_IGROUPR + i / 8, ~0u);
        wr32(gicd + GICD_ICENABLER + i / 8, ~0u);
        wr32(gicd + GICD_ICPENDR + i / 8, ~0u);
    }
    for (uint32_t i = GIC_INTID_SPI; i < gic_lines; i += 4)
        wr32(gicd + GICD_IPRIORITYR + i, PRIORITY_DEFAULT);
    for (uint32_t i = GIC_INTID_SPI; i < gic_lines; i++)
        wr64(gicd + GICD_IROUTER + 8 * i, route);
    wait_clear(gicd + GICD_CTLR, GICD_CTLR_RWP);
    // ARE has to be set on its own before the group enables.
    wr32(gicd + GICD_CTLR, GICD_CTLR_ARE);
    wait_clear(gicd + GICD_CTLR, GICD_CTLR_RWP);
    wr32(gicd + GICD_CTLR, GICD_CTLR_ARE | GICD_CTLR_G1A | GICD_CTLR_G1);
    wait_clear(gicd + GICD_CTLR, GICD_CTLR_RWP);
}

static void redistributor_init(void)
{
    // Wake it: a sleeping redistributor forwards nothing to the CPU.
    wr32(gicr + GICR_WAKER, rd32(gicr + GICR_WAKER) & ~GICR_WAKER_SLEEP);
    wait_clear(gicr + GICR_WAKER, GICR_WAKER_ASLEEP);

    uint64_t sgi = gicr + GICR_SGI;
    wr32(sgi + GICR_IGROUPR0, ~0u);
    wr32(sgi + GICR_IGRPMODR0, 0);
    wr32(sgi + GICR_ICENABLER0, ~0u);
    wr32(sgi + GICR_ICPENDR0, ~0u);
    for (uint32_t i = 0; i < 32; i += 4)
        wr32(sgi + GICR_IPRIORITYR + i, PRIORITY_DEFAULT);
    wait_clear(gicr + GICR_CTLR, GICR_CTLR_RWP);
}

static int cpu_interface_init(void)
{
    sysreg_write(ICC_SRE_EL1, sysreg_read(ICC_SRE_EL1) | 1);
    isb();
    if (!(sysreg_read(ICC_SRE_EL1) & 1))
        return -1;      // EL2 left ICC_SRE_EL2.Enable clear
    sysreg_write(ICC_PMR_EL1, 0xff);    // let every priority through
    sysreg_write(ICC_BPR1_EL1, 0);
    sysreg_write(ICC_CTLR_EL1, 0);      // EOImode 0: EOIR also deactivates
    sysreg_write(ICC_IGRPEN1_EL1, 1);
    isb();
    return 0;
}

int gic_init(const struct fdt* f)
{
    int node = fdt_find_compatible(f, -1, "arm,gic-v3");
    struct fdt_region reg;
    if (node < 0 || fdt_reg_cpu(f, node, 0, &reg) < 0) {
        console_printf("gic: no arm,gic-v3 in the device tree (GICv2? use -machine virt,gic-version=3)\n");
        return -1;
    }
    // ID_AA64PFR0_EL1.GIC [27:24]: the ICC_* system registers exist.
    if (!((sysreg_read("id_aa64pfr0_el1") >> 24) & 0xf)) {
        console_printf("gic: CPU has no GICv3 system register interface\n");
        return -1;
    }
    gicd = reg.base;
    intid_cells = fdt_getprop_u32(f, node, "#interrupt-cells", 3);

    uint32_t aff = cpu_affinity();
    int count;
    int index = find_redistributor(f, node, aff, &count);
    if (index < 0) {
        console_printf("gic: no redistributor for affinity 0x%x in %d frame(s)\n", aff, count);
        return -1;
    }
    distributor_init(aff);
    redistributor_init();
    if (cpu_interface_init() < 0) {
        console_printf("gic: ICC_SRE_EL1.SRE reads 0, system register interface disabled\n");
        return -1;
    }
    console_printf("gic: GICv3 distributor at 0x%lx, %u INTIDs\n", gicd, gic_lines);
    console_printf("gic: redistributor at 0x%lx (frame %d of %d, affinity 0x%x)\n", gicr, index, count, aff);
    return 0;
}

void gic_enable(uint32_t intid)
{
    if (intid < GIC_INTID_SPI)
        wr32(gicr + GICR_SGI + GICR_ISENABLER0, 1u << intid);
    else if (intid < gic_lines)
        wr32(gicd + GICD_ISENABLER + intid / 32 * 4, 1u << (intid % 32));
}

void gic_disable(uint32_t intid)
{
    if (intid < GIC_INTID_SPI) {
        wr32(gicr + GICR_SGI + GICR_ICENABLER0, 1u << intid);
        wait_clear(gicr + GICR_CTLR, GICR_CTLR_RWP);
    } else if (intid < gic_lines) {
        wr32(gicd + GICD_ICENABLER + intid / 32 * 4, 1u << (intid % 32));
        wait_clear(gicd + GICD_CTLR, GICD_CTLR_RWP);
    }
}

uint32_t gic_ack(void)
{
    return (uint32_t)sysreg_read(ICC_IAR1_EL1) & 0xffffff;
}

void gic_eoi(uint32_t intid)
{
    sysreg_write(ICC_EOIR1_EL1, intid);
}

static uint32_t be32(const uint8_t* p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

int gic_dt_intid(const struct fdt* f, int node, int index)
{
    uint32_t len;
    const uint8_t* p = fdt_getprop(f, node, "interrupts", &len);
    uint32_t cells = intid_cells ? intid_cells : 3;
    if (!p || cells < 2 || (uint32_t)(index + 1) * cells * 4 > len)
        return -1;
    p += index * cells * 4;
    uint32_t type = be32(p), num = be32(p + 4);
    return (int)(num + (type == 1 ? GIC_INTID_PPI : GIC_INTID_SPI));
}
