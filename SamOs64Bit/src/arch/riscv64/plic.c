// PLIC driver (sifive,plic-1.0.0 / riscv,plic0). Register
// offsets from the RISC-V PLIC specification. Mapped into the
// mmio window (mmu_map_mmio).
//
// One S-mode context per hart (plic_init_hart). A source is
// enabled in one context at a time, the boot hart's until plic_route
// moves it (arch_irq_set_cpu: a disk wait steers its line to the
// waiting hart, as x86's IOAPIC does).

#include "arch/riscv64/plic.h"
#include "arch/riscv64/console.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/riscv64.h"
#include "cpu/percpu.h"
#include "firmware/fdt.h"
#include "lib/spinlock/spinlock.h"

#define PLIC_PRIORITY(irq)   (4 * (irq))
#define PLIC_ENABLE(ctx)     (0x2000 + 0x80 * (ctx))
#define PLIC_THRESHOLD(ctx)  (0x200000 + 0x1000 * (ctx))
#define PLIC_CLAIM(ctx)      (0x200004 + 0x1000 * (ctx))

// Sources served; the spec allows 1023, QEMU virt has 95.
#define PLIC_MAX_IRQ 128

static uintptr_t plic_base;
static unsigned plic_ndev;
static int plic_node = -1;
static int boot_ctx = -1;
static INTERRUPT_CALLBACK_FUNCTION handlers[PLIC_MAX_IRQ];
static unsigned long counts[PLIC_MAX_IRQ];
// Each source's context plus one (0: the boot hart's) and
// whether it is enabled; the enable words are read-modify-write, so
// under a lock.
static int src_ctx[PLIC_MAX_IRQ];
static bool src_on[PLIC_MAX_IRQ];
static struct spinlock plic_lock = SPINLOCK_INIT("plic", LOCK_LEVEL_IOAPIC, 0);

static inline volatile uint32_t* plic_reg(uintptr_t off)
{
    return (volatile uint32_t*)(plic_base + off);
}

static uint32_t be32(const uint8_t* p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

// The context whose interrupts-extended entry is this hart's
// interrupt controller with cause 9 (supervisor external). QEMU
// numbers them 2 * hart + 1, but other boards (and harts without
// S-mode) do not, so the tree is the only safe source.
static int find_context(const struct fdt* f, int plic, int cpu)
{
    int intc = fdt_first_child(f, cpu);
    while (intc >= 0 && !fdt_node_is_compatible(f, intc, "riscv,cpu-intc")) {
        intc = fdt_next_sibling(f, intc);
    }
    uint32_t phandle = intc < 0 ? 0 : fdt_getprop_u32(f, intc, "phandle", 0);
    uint32_t len;
    const uint8_t* ie = fdt_getprop(f, plic, "interrupts-extended", &len);
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

int plic_init(const struct fdt* f, int cpu)
{
    int node = fdt_find_compatible(f, -1, "sifive,plic-1.0.0");
    if (node < 0) {
        node = fdt_find_compatible(f, -1, "riscv,plic0");
    }
    struct fdt_region reg;
    if (node < 0 || fdt_reg_cpu(f, node, 0, &reg) != 0) {
        console_printf("plic: none in the device tree\n");
        return -1;
    }
    // Through the mmio window, RW and never executable.
    plic_base = mmu_map_mmio(reg.base, reg.size);
    if (!plic_base) {
        console_printf("plic: cannot map 0x%lx\n", reg.base);
        return -1;
    }
    plic_ndev = fdt_getprop_u32(f, node, "riscv,ndev", 0);
    if (plic_ndev >= PLIC_MAX_IRQ) {
        plic_ndev = PLIC_MAX_IRQ - 1;
    }
    plic_node = node;
    boot_ctx = plic_init_hart(f, cpu);
    if (boot_ctx < 0) {
        console_printf("plic: no S-mode context for this hart\n");
        return -1;
    }
    // Priority 0 never interrupts: every source starts masked twice
    // over, by priority and by enable bit.
    for (unsigned irq = 1; irq <= plic_ndev; irq++) {
        *plic_reg(PLIC_PRIORITY(irq)) = 0;
    }
    console_printf("plic: 0x%lx, %u sources, context %d\n", reg.base, plic_ndev, boot_ctx);
    console_printf("plic: mapped at 0x%lx\n", plic_base);
    return 0;
}

// This hart's context: nothing enabled, threshold 0, SEIE.
int plic_init_hart(const struct fdt* f, int cpu)
{
    int ctx = plic_base ? find_context(f, plic_node, cpu) : -1;
    this_cpu()->arch.plic_ctx = ctx;
    if (ctx < 0) {
        return -1;
    }
    for (unsigned w = 0; w <= plic_ndev / 32; w++) {
        plic_reg(PLIC_ENABLE(ctx))[w] = 0;
    }
    *plic_reg(PLIC_THRESHOLD(ctx)) = 0;
    csr_set(sie, SIE_SEIE);
    return ctx;
}

static void enable_bit(int ctx, unsigned irq, bool on)
{
    volatile uint32_t* w = &plic_reg(PLIC_ENABLE(ctx))[irq / 32];
    *w = on ? *w | 1u << (irq % 32) : *w & ~(1u << (irq % 32));
}

static int ctx_of(unsigned irq)
{
    return src_ctx[irq] ? src_ctx[irq] - 1 : boot_ctx;
}

int plic_register_handler(unsigned irq, INTERRUPT_CALLBACK_FUNCTION handler)
{
    if (irq == 0 || irq > plic_ndev) {
        return -1;
    }
    handlers[irq] = handler;
    return 0;
}

void plic_enable(unsigned irq)
{
    if (irq == 0 || irq > plic_ndev || boot_ctx < 0) {
        return;
    }
    spin_lock(&plic_lock);
    *plic_reg(PLIC_PRIORITY(irq)) = 1;
    enable_bit(ctx_of(irq), irq, true);
    src_on[irq] = true;
    spin_unlock(&plic_lock);
}

void plic_disable(unsigned irq)
{
    if (irq == 0 || irq > plic_ndev || boot_ctx < 0) {
        return;
    }
    spin_lock(&plic_lock);
    enable_bit(ctx_of(irq), irq, false);
    src_on[irq] = false;
    spin_unlock(&plic_lock);
}

// Off in the old context, then on in the new one: a request
// pending at the gateway meanwhile goes to `ctx` once it is enabled.
int plic_route(unsigned irq, int ctx)
{
    if (irq == 0 || irq > plic_ndev || boot_ctx < 0 || ctx < 0) {
        return -1;
    }
    spin_lock(&plic_lock);
    if (src_on[irq]) {
        enable_bit(ctx_of(irq), irq, false);
        enable_bit(ctx, irq, true);
    }
    src_ctx[irq] = ctx + 1;
    spin_unlock(&plic_lock);
    return 0;
}

void plic_handle(struct trap_frame* frame)
{
    int ctx = this_cpu()->arch.plic_ctx;
    uint32_t irq;
    while ((irq = *plic_reg(PLIC_CLAIM(ctx))) != 0) {
        if (irq < PLIC_MAX_IRQ && handlers[irq]) {
            counts[irq]++;
            handlers[irq](frame);
        } else {
            // A source with no handler would come straight back.
            console_printf("plic: irq %u has no handler, disabled\n", irq);
            plic_disable(irq);
        }
        *plic_reg(PLIC_CLAIM(ctx)) = irq;
    }
}

unsigned long plic_count(unsigned irq)
{
    return irq < PLIC_MAX_IRQ ? counts[irq] : 0;
}
