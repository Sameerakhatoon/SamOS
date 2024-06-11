// GICv3 distributor, redistributor and CPU interface. See
// gicv3.h. Register names and offsets: ARM IHI 0069 (GICv3/v4).
//
// Works in either security view: QEMU virt without EL3 has a single
// security state (GICD_CTLR.DS = 1), real boards with TF-A give the
// kernel the non-secure view; the bits written below mean the same
// (ARE, then Group 1 enable) in both.
#include "arch/aarch64/gicv3.h"
#include "arch/aarch64/console.h"
#include "arch/aarch64/mmu.h"
#include "arch/aarch64/sysreg.h"
#include "cpu/percpu.h"

#define GICD_CTLR           0x0000
#define GICD_TYPER          0x0004
#define GICD_IGROUPR        0x0080
#define GICD_ISENABLER      0x0100
#define GICD_ICENABLER      0x0180
#define GICD_ICPENDR        0x0280
#define GICD_ICFGR          0x0c00
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
#define MAX_RD_FRAMES       64
#define GICD_CTLR_DS        (1u << 6)   // one security state: Group 0 is ours

static uint64_t gicd;           // distributor (virtual: Device memory)
static uint64_t gicr;           // the boot CPU's redistributor (RD_base, virtual)
static uint32_t gic_lines;      // INTIDs the distributor implements
static uint32_t intid_cells;    // #interrupt-cells
// Every redistributor frame and its affinity, for the other CPUs.
static uint64_t rd_frame[MAX_RD_FRAMES];
static uint32_t rd_aff[MAX_RD_FRAMES];
static int rd_frames;
// A GICv2 or the BCM2836 controller instead (gicv3.h).
static const struct gic_ops* other;

// SGIs and PPIs live in each CPU's own redistributor: struct
// cpu's (arch_start_cpus sets it), the boot CPU's before it has one.
static uint64_t this_gicr(void)
{
    struct cpu* c = arch_cpu_self();
    return c && c->arch.gicr ? c->arch.gicr : gicr;
}

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
        uint64_t va = mmu_map_device(reg.base, reg.size);
        if (!va)
            break;
        for (uint64_t rd = va; rd < va + reg.size;) {
            uint32_t lo = rd32(rd + GICR_TYPER), hi = rd32(rd + GICR_TYPER + 4);
            if (hi == aff && found < 0) {
                gicr = rd;
                found = *count;
            }
            if (*count < MAX_RD_FRAMES) {
                rd_frame[*count] = rd;
                rd_aff[*count] = hi;
                rd_frames = *count + 1;
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

static void redistributor_init(uint64_t gicr)
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
    if (node < 0) {
        int r = gicv2_probe(f, &other);
        if (r == 1)
            r = bcm2836_intc_probe(f, &other);
        if (r == 1)
            console_printf("gic: no GICv3, GICv2 or BCM2836 interrupt controller in the device tree\n");
        intid_cells = 3;
        return r == 0 ? 0 : -1;
    }
    if (fdt_reg_cpu(f, node, 0, &reg) < 0) {
        console_printf("gic: arm,gic-v3 without a distributor reg\n");
        return -1;
    }
    // ID_AA64PFR0_EL1.GIC [27:24]: the ICC_* system registers exist.
    if (!((sysreg_read("id_aa64pfr0_el1") >> 24) & 0xf)) {
        console_printf("gic: CPU has no GICv3 system register interface\n");
        return -1;
    }
    gicd = mmu_map_device(reg.base, reg.size);
    if (!gicd) {
        console_printf("gic: cannot map the distributor at 0x%lx\n", reg.base);
        return -1;
    }
    intid_cells = fdt_getprop_u32(f, node, "#interrupt-cells", 3);

    uint32_t aff = cpu_affinity();
    int count;
    int index = find_redistributor(f, node, aff, &count);
    if (index < 0) {
        console_printf("gic: no redistributor for affinity 0x%x in %d frame(s)\n", aff, count);
        return -1;
    }
    distributor_init(aff);
    redistributor_init(gicr);
    if (cpu_interface_init() < 0) {
        console_printf("gic: ICC_SRE_EL1.SRE reads 0, system register interface disabled\n");
        return -1;
    }
    console_printf("gic: GICv3 distributor at 0x%lx, %u INTIDs\n", virt_to_phys((void*)gicd), gic_lines);
    console_printf("gic: redistributor at 0x%lx (frame %d of %d, affinity 0x%x)\n", virt_to_phys((void*)gicr),
                   index, count, aff);
    return 0;
}

// Another CPU's half of gic_init: the redistributor frame
// arch_start_cpus found for it (gic_redistributor) and its CPU
// interface. 0, or -1 without the system register interface.
int gic_init_cpu(void)
{
    if (other)
        return other->init_cpu();
    redistributor_init(this_gicr());
    return cpu_interface_init();
}

uint64_t gic_redistributor(uint64_t mpidr)
{
    if (other)
        return other->cpu_token(mpidr);
    uint32_t aff = (uint32_t)(((mpidr >> 8) & 0xff000000) | (mpidr & 0xffffff));
    for (int i = 0; i < rd_frames; i++)
        if (rd_aff[i] == aff)
            return rd_frame[i];
    return 0;
}

int gic_rd_count(void)
{
    return rd_frames;
}

uint64_t gic_rd_frame(int i)
{
    return i >= 0 && i < rd_frames ? rd_frame[i] : 0;
}

void gic_enable(uint32_t intid)
{
    if (other)
        other->enable(intid);
    else if (intid < GIC_INTID_SPI)
        wr32(this_gicr() + GICR_SGI + GICR_ISENABLER0, 1u << intid);
    else if (intid < gic_lines)
        wr32(gicd + GICD_ISENABLER + intid / 32 * 4, 1u << (intid % 32));
}

void gic_disable(uint32_t intid)
{
    if (other) {
        other->disable(intid);
    } else if (intid < GIC_INTID_SPI) {
        wr32(this_gicr() + GICR_SGI + GICR_ICENABLER0, 1u << intid);
        wait_clear(this_gicr() + GICR_CTLR, GICR_CTLR_RWP);
    } else if (intid < gic_lines) {
        wr32(gicd + GICD_ICENABLER + intid / 32 * 4, 1u << (intid % 32));
        wait_clear(gicd + GICD_CTLR, GICD_CTLR_RWP);
    }
}

uint32_t gic_ack(void)
{
    if (other)
        return other->ack();
    return (uint32_t)sysreg_read(ICC_IAR1_EL1) & 0xffffff;
}

void gic_eoi(uint32_t intid)
{
    if (other) {
        other->eoi(intid);
        return;
    }
    sysreg_write(ICC_EOIR1_EL1, intid);
}

static uint32_t be32(const uint8_t* p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

int gic_dt_intid(const struct fdt* f, int node, int index)
{
    if (other && other->dt_intid)
        return other->dt_intid(f, node, index);
    uint32_t len;
    const uint8_t* p = fdt_getprop(f, node, "interrupts", &len);
    uint32_t cells = intid_cells ? intid_cells : 3;
    if (!p || cells < 2 || (uint32_t)(index + 1) * cells * 4 > len)
        return -1;
    p += index * cells * 4;
    uint32_t type = be32(p), num = be32(p + 4);
    return (int)(num + (type == 1 ? GIC_INTID_PPI : GIC_INTID_SPI));
}

// Device lines and SGIs.

void gic_set_priority(uint32_t intid, uint8_t priority)
{
    if (other)
        other->set_priority(intid, priority);
    else if (intid < GIC_INTID_SPI)
        *(volatile uint8_t*)(this_gicr() + GICR_SGI + GICR_IPRIORITYR + intid) = priority;
    else if (intid < gic_lines)
        *(volatile uint8_t*)(gicd + GICD_IPRIORITYR + intid) = priority;
}

// GICD_ICFGR: two bits per INTID, the upper one set for edge.
void gic_set_edge(uint32_t intid, int edge)
{
    if (other) {
        other->set_edge(intid, edge);
        return;
    }
    if (intid < GIC_INTID_SPI || intid >= gic_lines)
        return;
    uint64_t reg = gicd + GICD_ICFGR + intid / 16 * 4;
    uint32_t bit = 2u << (2 * (intid % 16));
    wr32(reg, edge ? rd32(reg) | bit : rd32(reg) & ~bit);
}

static uint64_t mpidr_route(uint64_t mpidr)
{
    return (mpidr & 0xff00ffffffull);           // Aff3, Aff2.Aff1.Aff0; IRM 0
}

void gic_route(uint32_t intid, uint64_t mpidr)
{
    if (other)
        other->route(intid, mpidr);
    else if (intid >= GIC_INTID_SPI && intid < gic_lines)
        wr64(gicd + GICD_IROUTER + 8 * intid, mpidr_route(mpidr));
}

// ICC_SGI1R_EL1: INTID [27:24], Aff3 [55:48], Aff2 [39:32], Aff1
// [23:16], RS [47:44] (Aff0 / 16) and the target list [15:0] (Aff0 % 16).
void gic_send_sgi(uint32_t sgi, uint64_t mpidr)
{
    if (other) {
        other->send_sgi(sgi, mpidr);
        return;
    }
    uint64_t aff0 = mpidr & 0xff;
    uint64_t v = ((uint64_t)(sgi & 0xf) << 24) | (((mpidr >> 32) & 0xff) << 48) | (((mpidr >> 16) & 0xff) << 32) |
                 (((mpidr >> 8) & 0xff) << 16) | ((aff0 >> 4) << 44) | (1ull << (aff0 & 0xf));
    __asm__ volatile("dsb ishst" ::: "memory");
    sysreg_write(ICC_SGI1R_EL1, v);
    isb();
}

// The panic stop as a Group 0 SGI, which the CPU takes as an
// FIQ: the kernel masks only IRQs (DAIF.I) around locks and in spin
// loops, so an FIQ reaches a CPU that an IRQ could not (x86: the NMI).
// Group 0 belongs to EL1 only with one security state (GICD_CTLR.DS,
// QEMU virt without secure=on); with TF-A it is EL3's, false then.
int gic_fiq_sgi(uint32_t sgi)
{
    if (other || !(rd32(gicd + GICD_CTLR) & GICD_CTLR_DS))
        return 0;
    uint64_t sgi_base = this_gicr() + GICR_SGI;
    wr32(sgi_base + GICR_IGROUPR0, rd32(sgi_base + GICR_IGROUPR0) & ~(1u << sgi));
    *(volatile uint8_t*)(sgi_base + GICR_IPRIORITYR + sgi) = 0;
    wr32(sgi_base + GICR_ISENABLER0, 1u << sgi);
    sysreg_write(ICC_IGRPEN0_EL1, 1);
    isb();
    return 1;
}

// ICC_SGI0R_EL1 with IRM (bit 40): every CPU but this one.
void gic_send_fiq_others(uint32_t sgi)
{
    __asm__ volatile("dsb ishst" ::: "memory");
    sysreg_write(ICC_SGI0R_EL1, ((uint64_t)(sgi & 0xf) << 24) | (1ull << 40));
    isb();
}

uint32_t gic_dt_flags(const struct fdt* f, int node, int index)
{
    if (other && other->dt_flags)
        return other->dt_flags(f, node, index);
    uint32_t len;
    const uint8_t* p = fdt_getprop(f, node, "interrupts", &len);
    uint32_t cells = intid_cells ? intid_cells : 3;
    if (!p || cells < 3 || (uint32_t)(index + 1) * cells * 4 > len)
        return 0;
    return be32(p + index * cells * 4 + 8);
}

uint32_t gic_get_pmr(void)
{
    return other ? other->get_pmr() : (uint32_t)sysreg_read(ICC_PMR_EL1);
}

void gic_set_pmr(uint32_t pmr)
{
    if (other)
        other->set_pmr(pmr);
    else
        sysreg_write(ICC_PMR_EL1, pmr);
}
