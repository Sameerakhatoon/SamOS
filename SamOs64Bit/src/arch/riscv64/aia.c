// Parity (riscv64 PCI) - the AIA: the S-level IMSIC (one interrupt
// file per hart, its registers behind siselect/sireg, claims through
// stopei) and the S-level APLIC in MSI delivery mode (aia.h).
// Register offsets from the RISC-V AIA specification 1.0.

#include "arch/riscv64/aia.h"
#include "arch/riscv64/console.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/riscv64.h"
#include "cpu/percpu.h"
#include "firmware/fdt.h"
#include "lib/spinlock/spinlock.h"
#include "status.h"

#define IMSIC_EIDELIVERY   0x70
#define IMSIC_EITHRESHOLD  0x72
#define IMSIC_EIE0         0xC0
#define IMSIC_PAGE         0x1000

#define APLIC_DOMAINCFG    0x0000
#define APLIC_SOURCECFG(s) (4 * (s))
#define APLIC_SETIPNUM     0x1CDC
#define APLIC_IN_CLRIP(s)  (0x1D00 + 4 * ((s) / 32))   // read: rectified inputs
#define APLIC_SETIENUM     0x1EDC
#define APLIC_CLRIENUM     0x1FDC
#define APLIC_TARGET(s)    (0x3000 + 4 * (s))
#define APLIC_DOMAINCFG_IE (1u << 8)
#define APLIC_DOMAINCFG_DM (1u << 2)     // MSI delivery
#define APLIC_SM_LEVEL1    6

#define EIID_MAX   255
#define EIID_WIRED 128                   // APLIC sources below, MSI slots from here

static uint64_t imsic_pa, imsic_size;
static int imsic_node = -1;
static unsigned num_ids;
static uintptr_t aplic_base;
static unsigned aplic_nsrc;
static bool aia_up;
static int boot_hart = -1;
static INTERRUPT_CALLBACK_FUNCTION handlers[EIID_MAX + 1];
static unsigned long counts[EIID_MAX + 1];
// Each source's target hart index plus one (0: the boot hart's) and
// whether it is enabled.
static int src_hart[EIID_WIRED];
static bool src_on[EIID_WIRED];
static struct spinlock aplic_lock = SPINLOCK_INIT("aplic", LOCK_LEVEL_IOAPIC, 0);

static inline volatile uint32_t* aplic_reg(uintptr_t off)
{
    return (volatile uint32_t*)(aplic_base + off);
}

static uint32_t be32(const uint8_t* p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void imsic_write(unsigned sel, uint64_t v)
{
    __asm__ volatile("csrw 0x150, %0; csrw 0x151, %1" : : "r"((uint64_t)sel), "r"(v) : "memory");
}

// Claim: read the highest pending enabled identity and clear it.
static unsigned imsic_claim(void)
{
    uint64_t v;
    __asm__ volatile("csrrw %0, 0x15C, zero" : "=r"(v) : : "memory");
    return (unsigned)(v >> 16) & 0x7FF;
}

// The entry of `node`'s interrupts-extended that is this hart's
// supervisor external interrupt: the hart's index in the IMSIC.
static int hart_index(const struct fdt* f, int node, int cpu)
{
    int intc = fdt_first_child(f, cpu);
    while (intc >= 0 && !fdt_node_is_compatible(f, intc, "riscv,cpu-intc")) {
        intc = fdt_next_sibling(f, intc);
    }
    uint32_t phandle = intc < 0 ? 0 : fdt_getprop_u32(f, intc, "phandle", 0);
    uint32_t len;
    const uint8_t* ie = fdt_getprop(f, node, "interrupts-extended", &len);
    if (!phandle || !ie) {
        return -1;
    }
    for (uint32_t i = 0; i + 8 <= len; i += 8) {
        if (be32(ie + i) == phandle && be32(ie + i + 4) == IRQ_S_EXT) {
            return i / 8;
        }
    }
    return -1;
}

// The S-level one: its interrupts-extended names cause 9, not 11.
static int find_imsic(const struct fdt* f)
{
    for (int n = fdt_find_compatible(f, -1, "riscv,imsics"); n >= 0; n = fdt_find_compatible(f, n, "riscv,imsics")) {
        uint32_t len;
        const uint8_t* ie = fdt_getprop(f, n, "interrupts-extended", &len);
        if (ie && len >= 8 && be32(ie + 4) == IRQ_S_EXT) {
            return n;
        }
    }
    return -1;
}

int aia_init(const struct fdt* f, int cpu)
{
    struct fdt_region reg;
    int node = find_imsic(f);
    if (node < 0 || fdt_reg_cpu(f, node, 0, &reg) != 0) {
        console_printf("aia: no S-level imsic in the device tree\n");
        return -1;
    }
    imsic_pa = reg.base;
    imsic_size = reg.size;
    imsic_node = node;
    num_ids = fdt_getprop_u32(f, node, "riscv,num-ids", 63);
    if (num_ids > EIID_MAX) {
        num_ids = EIID_MAX;
    }
    uint32_t phandle = fdt_getprop_u32(f, node, "phandle", 0);
    int aplic = -1;
    for (int n = fdt_find_compatible(f, -1, "riscv,aplic"); n >= 0; n = fdt_find_compatible(f, n, "riscv,aplic")) {
        if (fdt_getprop_u32(f, n, "msi-parent", 0) == phandle) {
            aplic = n;
            break;
        }
    }
    if (aplic >= 0 && fdt_reg_cpu(f, aplic, 0, &reg) == 0) {
        aplic_base = mmu_map_mmio(reg.base, reg.size);
        aplic_nsrc = fdt_getprop_u32(f, aplic, "riscv,num-sources", 0);
        if (aplic_nsrc >= EIID_WIRED) {
            aplic_nsrc = EIID_WIRED - 1;
        }
    }
    boot_hart = aia_init_hart(f, cpu);
    if (boot_hart < 0) {
        console_printf("aia: no imsic interrupt file for this hart\n");
        return -1;
    }
    if (aplic_base) {
        for (unsigned s = 1; s <= aplic_nsrc; s++) {
            *aplic_reg(APLIC_CLRIENUM) = s;
        }
        *aplic_reg(APLIC_DOMAINCFG) = APLIC_DOMAINCFG_IE | APLIC_DOMAINCFG_DM;
        console_printf("aplic: 0x%lx, %u sources, msi mode\n", reg.base, aplic_nsrc);
    } else {
        console_printf("aplic: none for this imsic, no wired interrupts\n");
    }
    console_printf("imsic: 0x%lx, %u ids, hart index %d\n", imsic_pa, num_ids, boot_hart);
    aia_up = true;
    return 0;
}

bool aia_active(void)
{
    return aia_up;
}

int aia_init_hart(const struct fdt* f, int cpu)
{
    int idx = imsic_node < 0 ? -1 : hart_index(f, imsic_node, cpu);
    if (idx >= 0 && (uint64_t)(idx + 1) * IMSIC_PAGE > imsic_size) {
        idx = -1;
    }
    this_cpu()->arch.imsic_hart = idx;
    if (idx < 0) {
        return -1;
    }
    imsic_write(IMSIC_EIDELIVERY, 0);
    imsic_write(IMSIC_EITHRESHOLD, 0);
    // RV64: only the even-numbered eie registers, 64 identities each.
    for (unsigned w = 0; w <= num_ids / 64; w++) {
        imsic_write(IMSIC_EIE0 + 2 * w, ~0ul);
    }
    imsic_write(IMSIC_EIDELIVERY, 1);
    csr_set(sie, SIE_SEIE);
    return idx;
}

int aia_register_handler(unsigned src, INTERRUPT_CALLBACK_FUNCTION handler)
{
    if (!aplic_base || src == 0 || src > aplic_nsrc) {
        return -1;
    }
    handlers[src] = handler;
    return 0;
}

static void set_target(unsigned src, int hart)
{
    *aplic_reg(APLIC_TARGET(src)) = (uint32_t)hart << 18 | src;
}

void aia_enable(unsigned src)
{
    if (!aplic_base || src == 0 || src > aplic_nsrc) {
        return;
    }
    spin_lock(&aplic_lock);
    if (!src_on[src]) {
        *aplic_reg(APLIC_SOURCECFG(src)) = APLIC_SM_LEVEL1;
        set_target(src, src_hart[src] ? src_hart[src] - 1 : boot_hart);
    }
    *aplic_reg(APLIC_SETIENUM) = src;
    src_on[src] = true;
    spin_unlock(&aplic_lock);
}

void aia_disable(unsigned src)
{
    if (!aplic_base || src == 0 || src > aplic_nsrc) {
        return;
    }
    spin_lock(&aplic_lock);
    *aplic_reg(APLIC_CLRIENUM) = src;
    src_on[src] = false;
    spin_unlock(&aplic_lock);
}

// Off, the new target, on again: a request pending meanwhile stays
// pending in the APLIC and goes to the new hart.
int aia_route(unsigned src, struct cpu* c)
{
    if (!aplic_base || src == 0 || src > aplic_nsrc || c->arch.imsic_hart < 0) {
        return -1;
    }
    spin_lock(&aplic_lock);
    *aplic_reg(APLIC_CLRIENUM) = src;
    set_target(src, c->arch.imsic_hart);
    src_hart[src] = c->arch.imsic_hart + 1;
    if (src_on[src]) {
        *aplic_reg(APLIC_SETIENUM) = src;
    }
    spin_unlock(&aplic_lock);
    return 0;
}

int aia_msi(unsigned slot, struct cpu* c, INTERRUPT_CALLBACK_FUNCTION handler,
            uint64_t* addr, uint32_t* data)
{
    unsigned eiid = EIID_WIRED + slot;
    if (!aia_up || eiid > num_ids || c->arch.imsic_hart < 0) {
        return -EINVARG;
    }
    if (handler) {
        handlers[eiid] = handler;
    }
    *addr = imsic_pa + (uint64_t)c->arch.imsic_hart * IMSIC_PAGE;
    *data = eiid;
    return (int)eiid;
}

void aia_handle(struct trap_frame* frame)
{
    unsigned id;
    while ((id = imsic_claim()) != 0) {
        if (handlers[id]) {
            counts[id]++;
            handlers[id](frame);
        } else {
            console_printf("imsic: eiid %u has no handler\n", id);
        }
        if (id < EIID_WIRED && aplic_base) {
            if (!handlers[id]) {
                aia_disable(id);
            } else if (src_on[id] && (*aplic_reg(APLIC_IN_CLRIP(id)) & 1u << id % 32)) {
                // Only while the line is still up: QEMU sets a level
                // source pending on setipnum whatever its input.
                *aplic_reg(APLIC_SETIPNUM) = id;
            }
        }
    }
}

unsigned long aia_count(unsigned eiid)
{
    return eiid <= EIID_MAX ? counts[eiid] : 0;
}
