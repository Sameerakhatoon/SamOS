// S-PAR - the GICv3 ITS: PCI MSI and MSI-X as LPIs (its.h). ARM IHI
// 0069 chapters 5 (LPIs) and 6 (the ITS, its commands).
//
// Tables, all from the PFA, zeroed, Normal inner-shareable write-back
// (each line also cleaned to the point of coherency, for an ITS that
// does not snoop):
//   - LPI configuration: one for every redistributor (GICR_PROPBASER),
//     a byte per LPI (priority, enable), INTIDs below 1 << ITS_ID_BITS;
//   - LPI pending: one per redistributor (GICR_PENDBASER), 64 KiB
//     aligned;
//   - the ITS's device and collection tables (GITS_BASER<n>, flat);
//   - an interrupt translation table per device (MAPD), 32 events;
//   - the command queue, one page (GITS_CBASER), under its_lock.
// Collection i is redistributor frame i (gic_rd_frame), mapped at init
// (MAPC) for every frame, so an LPI can be moved (MOVI) to a CPU that
// has not started yet. LPIs are enabled on every redistributor at init
// for the same reason.
#include "arch/aarch64/its.h"
#include "arch/aarch64/gicv3.h"
#include "arch/aarch64/console.h"
#include "arch/aarch64/mmu.h"
#include "arch/arch.h"
#include "lib/spinlock/spinlock.h"
#include "memory/memory.h"
#include "memory/pfa.h"
#include "status.h"

#define GITS_CTLR           0x0000
#define GITS_TYPER          0x0008
#define GITS_CBASER         0x0080
#define GITS_CWRITER        0x0088
#define GITS_CREADR         0x0090
#define GITS_BASER          0x0100
#define GITS_TRANSLATER     0x10040
#define GITS_CTLR_ENABLED   (1u << 0)
#define GITS_CTLR_QUIESCENT (1u << 31)

#define GICR_CTLR           0x0000
#define GICR_TYPER          0x0008
#define GICR_PROPBASER      0x0070
#define GICR_PENDBASER      0x0078
#define GICR_CTLR_LPIS      (1u << 0)
#define GICR_TYPER_PLPIS    (1u << 0)

// GITS_BASER, GITS_CBASER, GICR_PROPBASER and PENDBASER share these.
#define BASER_VALID         (1ull << 63)
#define BASER_INNER_WB      (7ull << 59)        // RaWaWb (GITS_*: 61:59)
#define BASER_SHARE_INNER   (1ull << 10)
#define GICR_BASER_INNER_WB (7ull << 7)         // GICR_*: 9:7
#define BASER_TYPE(v)       (((v) >> 56) & 7)
#define BASER_ESZ(v)        ((((v) >> 48) & 0x1f) + 1)
#define BASER_DEVICE        1
#define BASER_COLLECTION    4
#define BASER_MAX_PAGES     256                 // Size field, 4 KiB pages

#define CMD_MOVI            0x01
#define CMD_SYNC            0x05
#define CMD_MAPD            0x08
#define CMD_MAPC            0x09
#define CMD_MAPTI           0x0a
#define CMD_INV             0x0c
#define CMD_QUEUE_BYTES     PAGE_SIZE

#define ITS_ID_BITS         14                  // LPIs 8192..16383
#define ITS_EVENT_BITS      5
#define ITS_MAX_DEVICES     8
#define LPI_CFG_BYTES       ((1u << ITS_ID_BITS) - GIC_INTID_LPI)
#define LPI_CFG_RES1        0x02
#define LPI_CFG_ENABLE      0x01
#define PENDING_ALIGN       0x10000

static uint64_t its;            // GITS registers (virtual)
static uint64_t its_pa;
static int its_pta;             // GITS_TYPER.PTA: collections target addresses
static uint8_t* lpi_cfg;
static uint64_t* cmdq;
static uint32_t cmd_next;       // byte offset of the next command
static struct spinlock its_lock = SPINLOCK_INIT("its", LOCK_LEVEL_IOAPIC, false);
static struct {
    uint32_t id;
    uint32_t mapped;            // events with a MAPTI done
} dev[ITS_MAX_DEVICES];
static int devs;

static inline uint64_t rd64(uint64_t a) { return *(volatile uint64_t*)a; }
static inline uint32_t rd32(uint64_t a) { return *(volatile uint32_t*)a; }
static inline void wr64(uint64_t a, uint64_t v) { *(volatile uint64_t*)a = v; }
static inline void wr32(uint64_t a, uint32_t v) { *(volatile uint32_t*)a = v; }

static void clean(const void* p, size_t len)
{
    for (uint64_t a = (uint64_t)p & ~63ull; a < (uint64_t)p + len; a += 64)
        __asm__ volatile("dc civac, %0" :: "r"(a) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
}

static void* table(size_t pages)
{
    void* p = pfa_alloc_frames(pages);
    if (p) {
        memset(p, 0, pages * PAGE_SIZE);
        clean(p, pages * PAGE_SIZE);
    }
    return p;
}

// Collection of a redistributor frame: what MAPC and SYNC name it by.
static uint64_t rd_target(int i)
{
    uint64_t rd = gic_rd_frame(i);
    return its_pta ? virt_to_phys((void*)rd) >> 16 : (rd32(rd + GICR_TYPER) >> 8) & 0xffff;
}

static int rd_index(uint64_t gicr)
{
    for (int i = 0; i < gic_rd_count(); i++)
        if (gic_rd_frame(i) == gicr)
            return i;
    return -1;
}

// One command, then wait for the ITS to have read it. its_lock held.
static int its_cmd(uint64_t d0, uint64_t d1, uint64_t d2)
{
    uint64_t* c = (uint64_t*)((uint8_t*)cmdq + cmd_next);
    c[0] = d0;
    c[1] = d1;
    c[2] = d2;
    c[3] = 0;
    clean(c, 32);
    cmd_next = (cmd_next + 32) % CMD_QUEUE_BYTES;
    wr64(its + GITS_CWRITER, cmd_next);
    for (uint64_t end = arch_time_us() + 1000000; rd64(its + GITS_CREADR) != cmd_next;) {
        if (rd64(its + GITS_CREADR) & 1 || arch_time_us() > end) {
            console_printf("its: command 0x%lx stalled (CREADR 0x%lx)\n", d0 & 0xff, rd64(its + GITS_CREADR));
            return -EIO;
        }
    }
    return 0;
}

static int sync_rd(int i)
{
    return its_cmd(CMD_SYNC, 0, rd_target(i) << 16);
}

// Each GITS_BASER the ITS wants memory for: the device table for every
// DeviceID it decodes, the collection table for every redistributor.
static int baser_init(uint64_t typer)
{
    for (int n = 0; n < 8; n++) {
        uint64_t reg = its + GITS_BASER + 8 * n, v = rd64(reg);
        uint64_t type = BASER_TYPE(v), esz = BASER_ESZ(v), entries;
        if (type == BASER_DEVICE)
            entries = 1ull << (((typer >> 13) & 0x1f) + 1);
        else if (type == BASER_COLLECTION)
            entries = (uint64_t)gic_rd_count();
        else
            continue;
        uint64_t pages = (entries * esz + PAGE_SIZE - 1) / PAGE_SIZE;
        void* t = pages <= BASER_MAX_PAGES ? table(pages) : NULL;
        if (!t) {
            console_printf("its: no memory for a %lu-page table (type %lu)\n", pages, type);
            return -ENOMEM;
        }
        wr64(reg, BASER_VALID | BASER_INNER_WB | (v & (7ull << 56)) | ((esz - 1) << 48) | BASER_SHARE_INNER |
                  virt_to_phys(t) | (pages - 1));
    }
    return 0;
}

static int rd_lpis_init(void)
{
    int n = gic_rd_count();
    uint8_t* pend = table((size_t)n * (PENDING_ALIGN / PAGE_SIZE) + PENDING_ALIGN / PAGE_SIZE - 1);
    lpi_cfg = table(LPI_CFG_BYTES / PAGE_SIZE);
    if (!pend || !lpi_cfg)
        return -ENOMEM;
    for (uint32_t i = 0; i < LPI_CFG_BYTES; i++)
        lpi_cfg[i] = LPI_CFG_RES1 | 0xa0;
    clean(lpi_cfg, LPI_CFG_BYTES);
    uint64_t pend_pa = (virt_to_phys(pend) + PENDING_ALIGN - 1) & ~(uint64_t)(PENDING_ALIGN - 1);
    for (int i = 0; i < n; i++) {
        uint64_t rd = gic_rd_frame(i);
        if (!(rd32(rd + GICR_TYPER) & GICR_TYPER_PLPIS) || rd32(rd + GICR_CTLR) & GICR_CTLR_LPIS)
            return -EIO;        // no LPIs, or someone enabled them with tables of its own
        wr64(rd + GICR_PROPBASER, virt_to_phys(lpi_cfg) | GICR_BASER_INNER_WB | BASER_SHARE_INNER |
                                  (ITS_ID_BITS - 1));
        wr64(rd + GICR_PENDBASER, (pend_pa + (uint64_t)i * PENDING_ALIGN) | GICR_BASER_INNER_WB |
                                  BASER_SHARE_INNER);
        __asm__ volatile("dsb sy" ::: "memory");
        wr32(rd + GICR_CTLR, rd32(rd + GICR_CTLR) | GICR_CTLR_LPIS);
    }
    return 0;
}

int its_init(const struct fdt* f)
{
    // A GICv2 or the BCM2836 (gic_ops) has no redistributors and
    // so no LPIs: no ITS, arch_irq_msi refuses, PCI takes INTx.
    if (gic_rd_count() == 0)
        return -ENOENT;
    int node = fdt_find_compatible(f, -1, "arm,gic-v3-its");
    struct fdt_region reg;
    if (node < 0 || fdt_reg_cpu(f, node, 0, &reg) < 0) {
        console_printf("its: none in the device tree, PCI MSI off (INTx)\n");
        return -ENOENT;
    }
    its = mmu_map_device(reg.base, reg.size);
    if (!its)
        return -ENOMEM;
    its_pa = reg.base;
    wr32(its + GITS_CTLR, 0);
    while (!(rd32(its + GITS_CTLR) & GITS_CTLR_QUIESCENT))
        ;
    uint64_t typer = rd64(its + GITS_TYPER);
    its_pta = (typer >> 19) & 1;
    cmdq = table(CMD_QUEUE_BYTES / PAGE_SIZE);
    int r = !cmdq ? -ENOMEM : baser_init(typer);
    if (r == 0)
        r = rd_lpis_init();
    if (r < 0) {
        console_printf("its: set-up failed (%d), PCI MSI off (INTx)\n", r);
        its = 0;
        return r;
    }
    wr64(its + GITS_CBASER, BASER_VALID | BASER_INNER_WB | BASER_SHARE_INNER | virt_to_phys(cmdq) |
                            (CMD_QUEUE_BYTES / PAGE_SIZE - 1));
    wr64(its + GITS_CWRITER, 0);
    wr32(its + GITS_CTLR, GITS_CTLR_ENABLED);
    spin_lock(&its_lock);
    for (int i = 0; i < gic_rd_count() && r == 0; i++)
        r = its_cmd(CMD_MAPC, 0, BASER_VALID | rd_target(i) << 16 | (uint64_t)i);
    if (r == 0)
        r = sync_rd(0);
    spin_unlock(&its_lock);
    if (r < 0) {
        its = 0;
        return r;
    }
    console_printf("its: GICv3 ITS at 0x%lx, %d collection(s), LPIs %u-%u, doorbell 0x%lx\n", its_pa,
                   gic_rd_count(), GIC_INTID_LPI, GIC_INTID_LPI + GIC_LPIS - 1, its_pa + GITS_TRANSLATER);
    return 0;
}

bool its_ready(void)
{
    return its != 0;
}

uint64_t its_doorbell(void)
{
    return its_pa + GITS_TRANSLATER;
}

int its_map(uint32_t devid, uint32_t event, uint32_t lpi, uint64_t gicr, uint8_t prio)
{
    int col = rd_index(gicr);
    if (!its || col < 0 || event >= (1u << ITS_EVENT_BITS) || lpi < GIC_INTID_LPI ||
        lpi >= GIC_INTID_LPI + GIC_LPIS)
        return -EINVARG;
    // A new device's translation table, outside the lock (the PFA's).
    void* itt = NULL;
    int d;
    for (d = 0; d < devs && dev[d].id != devid; d++)
        ;
    if (d == devs && (d == ITS_MAX_DEVICES || !(itt = table(1))))
        return -ENOMEM;
    spin_lock(&its_lock);
    int r = 0;
    if (d == devs) {
        dev[d].id = devid;
        dev[d].mapped = 0;
        devs++;
        r = its_cmd(CMD_MAPD | (uint64_t)devid << 32, ITS_EVENT_BITS - 1, BASER_VALID | virt_to_phys(itt));
    }
    if (r == 0 && !(dev[d].mapped & (1u << event))) {
        lpi_cfg[lpi - GIC_INTID_LPI] = (prio & 0xfc) | LPI_CFG_RES1 | LPI_CFG_ENABLE;
        clean(&lpi_cfg[lpi - GIC_INTID_LPI], 1);
        r = its_cmd(CMD_MAPTI | (uint64_t)devid << 32, event | (uint64_t)lpi << 32, col);
        if (r == 0)
            r = its_cmd(CMD_INV | (uint64_t)devid << 32, event, 0);
        if (r == 0)
            dev[d].mapped |= 1u << event;
    } else if (r == 0) {
        r = its_cmd(CMD_MOVI | (uint64_t)devid << 32, event, col);
    }
    if (r == 0)
        r = sync_rd(col);
    spin_unlock(&its_lock);
    return r;
}
