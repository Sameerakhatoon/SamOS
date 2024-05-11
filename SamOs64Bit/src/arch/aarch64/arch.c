// aarch64's side of arch/arch.h (the MMU half is paging.c, the
// user copies uaccess.c), and the hand-over from the early aarch64 boot
// (main.c) to the shared kernel_main.
//
// aarch64_enter_kernel builds what x86's UEFI loader gives kernel_main:
// a struct samos_boot_info with the bootargs as the command line, and
// an E820-style memory map (count, then entries) made from the device
// tree's memory map (memmap.c) and the frames mmu_early's allocator
// handed out. kheap_init's PFA (memory/pfa.c) then manages RAM from
// that map, the same code as on x86 (the hardware layer memory map note).
//
// No port I/O and no legacy PCI configuration mechanism. The
// other CPUs start with PSCI CPU_ON (arch_start_cpus, below).
//
// Devices. arch_firmware_init lists the device tree's
// virtio-mmio slots (virtio_bus.h), highest address first, for
// virtio-blk and virtio-input, and sets up the screen: QEMU's ramfb,
// a framebuffer in RAM from the PFA described to QEMU through fw_cfg,
// handed to graphics_setup in boot_info as x86's loader hands over
// the GOP's. Device completion lines
// (arch_irq_attach_gsi) are GIC SPIs at priority PRIO_COMPLETION with
// their EOI after the handler; a disk wait sets ICC_PMR_EL1 so that
// only they are taken (x86: the LAPIC TPR). IPIs are SGIs 0-3 (the
// enum arch_ipi), which the wait holds off like the timer. Bytes
// received on the PL011 are keys (keyboard_push), as PS/2 keys are on
// x86.

#include "arch/arch.h"
#include "acpi/acpi.h"
#include "arch/aarch64/console.h"
#include "arch/aarch64/gicv3.h"
#include "arch/aarch64/memmap.h"
#include "arch/aarch64/mmu.h"
#include "arch/aarch64/pfa.h"
#include "arch/aarch64/pl011.h"
#include "arch/aarch64/sysreg.h"
#include "arch/aarch64/timer.h"
#include "arch/aarch64/trap.h"
#include "boot/boot_info.h"
#include "boot/cmdline.h"
#include "bootmarker.h"
#include "cpu/percpu.h"
#include "cpu/smp.h"
#include "disk/drivers/ramdisk.h"
#include "firmware/fdt.h"
#include "firmware/ramfb.h"
#include "graphics/graphics.h"
#include "io/pci.h"
#include "io/rtc.h"
#include "io/serial.h"
#include "kernel.h"
#include "keyboard/keyboard.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "memory/pfa.h"
#include "string/string.h"
#include "status.h"
#include "task/task.h"
#include "virtio/virtio_mmio_fdt.h"

#define E820_MAX        64
#define EARLY_RUNS      32
#define PSCI_SYSTEM_OFF   0x84000008
#define PSCI_SYSTEM_RESET 0x84000009
#define ID_PAN_SHIFT    20          // ID_AA64MMFR1_EL1.PAN
#define SCTLR_SPAN      (1ul << 23) // 0: PSTATE.PAN set on every exception to EL1

// Without a ramfb device: a framebuffer format the graphics code
// refuses, so it draws into RAM only.
#define FB_FORMAT_NONE  2
#define FB_WIDTH        1024
#define FB_HEIGHT       768
#define FB_BYTES        (FB_WIDTH * FB_HEIGHT * 4)

#define PRIO_COMPLETION 0x80        // device completion lines; everything else 0xa0
#define PMR_DISK_WAIT   0x90        // a disk wait takes only the completion lines
#define IPI_SGIS        4           // SGI n is enum arch_ipi n

struct samos_boot_info* samos_boot_info_ptr;
struct graphics_info default_graphics_info;
uint64_t aarch64_boot_markers[BM_STAGE_MAX] __attribute__((aligned(4096)));

extern char _head[], _end[], _stack_bottom[], _stack_top[];
void aarch64_task_return(struct trap_frame* f, uint64_t kernel_stack_top) __attribute__((noreturn));

static struct samos_boot_info boot_info;
static struct {
    uint64_t          count;
    struct e820_entry entries[E820_MAX];
} __attribute__((packed, aligned(8))) e820;
static struct fdt dt;
static int dt_ok;
static int gic_ok;
static uint64_t boot_counter;
static int psci_smc;                // PSCI through smc, else hvc
static struct ramfb_dma ramfb_dma;

static void cpu_irqs_init(void);

// ------------------------------------------------------------------
// The hand-over: boot_info, the memory map, this CPU, kernel_main
// ------------------------------------------------------------------

static int e820_add(uint64_t base, uint64_t length, uint32_t type)
{
    if (e820.count == E820_MAX)
        return -1;
    struct e820_entry* e = &e820.entries[e820.count++];
    e->base_addr = base;
    e->length = length;
    e->type = type;
    e->extended_attr = 0;
    return 0;
}

// Usable RAM minus every reservation, as E820 type 1 runs, then each
// reservation as an entry of its own: the E820 rule (no overlap with
// usable RAM) that pfa.c relies on, which the device tree's list of
// memory plus reservations does not follow.
static int build_memory_map(void)
{
    static struct mem_region res[MEMMAP_MAX + EARLY_RUNS];
    int n, nres = 0;
    const struct mem_region* m = memmap_regions(&n);
    for (int i = 0; i < n; i++)
        if (m[i].type != MEM_REGION_USABLE)
            res[nres++] = m[i];
    int runs = early_pfa_used_runs(&res[nres], EARLY_RUNS);
    if (runs < 0)
        return -1;
    nres += runs;
    for (int i = 0; i < nres; i++) {        // whole pages
        uint64_t s = res[i].base & ~(PAGE_SIZE - 1);
        uint64_t e = (res[i].base + res[i].length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        res[i].base = s;
        res[i].length = e - s;
    }
    for (int i = 0; i < n; i++) {
        if (m[i].type != MEM_REGION_USABLE)
            continue;
        uint64_t cur = m[i].base, end = m[i].base + m[i].length;
        while (cur < end) {
            uint64_t next = end;
            int inside = 0;
            for (int k = 0; k < nres; k++) {
                if (res[k].base <= cur && cur < res[k].base + res[k].length) {
                    cur = res[k].base + res[k].length;
                    inside = 1;
                    break;
                }
                if (res[k].base > cur && res[k].base < next)
                    next = res[k].base;
            }
            if (inside)
                continue;
            if (e820_add(cur, next - cur, SAMOS_E820_TYPE_USABLE) < 0)
                return -1;
            cur = next;
        }
    }
    for (int i = 0; i < nres; i++) {
        uint32_t type = (res[i].type & ~MEMMAP_NOMAP) == MEM_REGION_KERNEL ? SAMOS_E820_TYPE_KERNEL
                                                                          : SAMOS_E820_TYPE_RESERVED;
        if (e820_add(res[i].base, res[i].length, type) < 0)
            return -1;
    }
    return 0;
}

static void psci_init(void)
{
    int node = fdt_find_compatible(&dt, -1, "arm,psci-1.0");
    if (node < 0)
        node = fdt_find_compatible(&dt, -1, "arm,psci-0.2");
    const char* method = node >= 0 ? fdt_getprop_string(&dt, node, "method") : 0;
    psci_smc = method && method[0] == 's';
}

void aarch64_enter_kernel(const struct fdt* f, uint64_t dtb_pa, const char* bootargs)
{
    dt = *f;
    dt_ok = 1;
    psci_init();

    // This CPU's struct cpu, before anything takes a lock.
    struct cpu* c = &cpus[0];
    c->self = c;
    c->index = 0;
    c->online = 1;
    c->arch.mpidr = sysreg_read("mpidr_el1") & 0xff00ffffffull;
    c->hw_id = (uint32_t)c->arch.mpidr;
    c->arch.stack_bottom = (uint8_t*)_stack_bottom;
    c->arch.kernel_stack = (uint8_t*)_stack_top;
    c->arch.gicr = gic_redistributor(c->arch.mpidr);
    sysreg_write("tpidr_el1", (uint64_t)c);

    if (build_memory_map() < 0) {
        console_printf("boot: memory map does not fit %d entries, halted\n", E820_MAX);
        aarch64_halt();
    }
    boot_info.magic = SAMOS_BOOT_INFO_MAGIC;
    boot_info.version = SAMOS_BOOT_INFO_VERSION;
    boot_info.size = sizeof(boot_info);
    boot_info.fb_format = FB_FORMAT_NONE;
    boot_info.memory_map = kimage_pa(&e820);
    boot_info.kernel_start = kimage_pa(_head);
    boot_info.kernel_end = kimage_pa(_end);
    for (int i = 0; bootargs && bootargs[i] && i < SAMOS_BOOT_CMDLINE_MAX - 1; i++)
        boot_info.cmdline[i] = bootargs[i];
    samos_boot_info_ptr = &boot_info;

    default_graphics_info.horizontal_resolution = FB_WIDTH;
    default_graphics_info.vertical_resolution = FB_HEIGHT;
    default_graphics_info.pixels_per_scanline = FB_WIDTH;

    console_printf("boot: kernel_main, %lu memory map entries, dtb 0x%lx, boot markers at pa 0x%lx\n",
                   e820.count, dtb_pa, kimage_pa(aarch64_boot_markers));
    kernel_main();
    aarch64_halt();
}

// ------------------------------------------------------------------
// Boot, CPU set-up, halting
// ------------------------------------------------------------------

// PAN (ARMv8.1): with SCTLR_EL1.SPAN clear every exception to EL1 sets
// PSTATE.PAN, so the kernel's own loads and stores cannot touch a page
// EL0 may access; only uaccess.S's LDTR/STTR do (they check EL0's
// permissions). A Cortex-A72 is v8.0 and has no PAN; there the user
// half is still out of the kernel's way (TTBR0 holds the empty table
// in the kernel, kernel_page), and the copies still go through LDTR.
void arch_early_init(void)
{
    boot_counter = counter_read();
    if ((sysreg_read("id_aa64mmfr1_el1") >> ID_PAN_SHIFT) & 0xf) {
        sysreg_write("sctlr_el1", sysreg_read("sctlr_el1") & ~SCTLR_SPAN);
        __asm__ volatile(".inst 0xd500419f" ::: "memory");     // msr pan, #1
        isb();
        serial_write((sysreg_read("S3_0_C4_C2_3") >> 22) & 1 ? "cpu: PAN on (PSTATE.PAN 1, SPAN 0)\n"
                                                              : "cpu: PAN did not turn on\n");
    } else {
        serial_write("cpu: no PAN (ARMv8.0), user memory only through LDTR/STTR\n");
    }
}

// The vectors are VBAR_EL1 since boot.S. Traps from EL0 start at the
// top of the boot stack (kernel_main never returns, so nothing there
// is in use once the first task runs).
void arch_trap_init(void)
{
    this_cpu()->arch.kernel_stack = (uint8_t*)_stack_top;
    trap_enable_tasks();
}

void arch_halt(void)
{
    aarch64_halt();
}

void arch_wait_for_interrupt(void)
{
    __asm__ volatile("msr daifclr, #2\n\twfi\n\tmsr daifset, #2" ::: "memory");
}

void arch_dump_frame(struct trap_frame* f)
{
    for (int i = 0; i < 31; i++)
        console_printf("  x%d%s 0x%016lx%s", i, i < 10 ? " " : "", f->x[i], i % 3 == 2 || i == 30 ? "\n" : "");
    console_printf("  sp  0x%016lx elr 0x%016lx spsr 0x%lx\n", f->sp, f->elr, f->spsr);
}

// No architectural bit says so; QEMU's virt machine names itself in
// the root node.
bool arch_on_hypervisor(void)
{
    return dt_ok && fdt_node_is_compatible(&dt, 0, "linux,dummy-virt");
}

bool arch_cpu_identified(void)
{
    return sysreg_read("midr_el1") != 0;
}

void arch_fault_test(int kind)
{
    if (kind == ARCH_FAULT_TEST_UNDEFINED)
        __asm__ volatile("udf #0x2a");
    else if (kind == ARCH_FAULT_TEST_BAD_STACK)
        __asm__ volatile("mov x1, #0x7ff000000000\n\tmov sp, x1\n\tstr x0, [sp, #-16]!" ::: "x1", "memory");
}

// ------------------------------------------------------------------
// Console: the PL011 main.c set up
// ------------------------------------------------------------------

bool arch_console_init(void)
{
    return pl011_base() != 0;
}

void arch_console_putc(char c)
{
    pl011_putc(c);
}

// ------------------------------------------------------------------
// Traps and the return to EL0
// ------------------------------------------------------------------

void arch_kernel_context(void)
{
}

void arch_user_context(void)
{
}

void arch_task_return(struct trap_frame* frame)
{
    aarch64_task_return(frame, (uint64_t)this_cpu()->arch.kernel_stack);
}

// ------------------------------------------------------------------
// Interrupt controller: GICv3 (main.c brought it up before hello)
// ------------------------------------------------------------------

// SGI n is IPI n (enum arch_ipi).
#define SGI_HANDLER(n) \
    static void sgi##n(struct trap_frame* f) { smp_handle_ipi((enum arch_ipi)n, f); }
SGI_HANDLER(0) SGI_HANDLER(1) SGI_HANDLER(2) SGI_HANDLER(3)
static INTERRUPT_CALLBACK_FUNCTION const sgi_handler[IPI_SGIS] = { sgi0, sgi1, sgi2, sgi3 };

// Bytes from the serial line are keys. A terminal sends CR for Enter
// and DEL for Backspace; the PS/2 driver pushes CR and BS, so DEL
// becomes BS, and LF (from a pipe) CR.
static void uart_irq(struct trap_frame* f)
{
    int c;
    while ((c = pl011_getc()) >= 0) {
        if (c == '\n')
            c = '\r';
        else if (c == 0x7f)
            c = 0x08;
        keyboard_push((char)c);
    }
}

static void uart_rx_init(void)
{
    int node = fdt_stdout_node(&dt);
    if (node < 0 || !fdt_node_is_compatible(&dt, node, "arm,pl011"))
        node = fdt_find_compatible(&dt, -1, "arm,pl011");
    int intid = node >= 0 && pl011_base() ? gic_dt_intid(&dt, node, 0) : -1;
    if (intid < GIC_INTID_SPI) {
        serial_write("console: no PL011 interrupt, no keys from the serial line\n");
        return;
    }
    gic_set_edge((uint32_t)intid, (gic_dt_flags(&dt, node, 0) & 3) != 0);
    irq_register((uint32_t)intid, uart_irq);
    pl011_rx_enable();
    gic_enable((uint32_t)intid);
    console_printf("console: PL011 receive on INTID %d, bytes are keys\n", intid);
}

void arch_irq_init(void)
{
    if (!gic_ok && dt_ok)
        gic_ok = gic_init(&dt) == 0;
    if (!gic_ok)
        return;
    for (uint32_t i = 0; i < IPI_SGIS; i++)
        irq_register(i, sgi_handler[i]);
    cpu_irqs_init();
    uart_rx_init();
}

void aarch64_gic_ready(void)
{
    gic_ok = 1;
}

bool arch_irq_routable(void)
{
    return gic_ok;
}

// IRQ numbers here are INTIDs. The SGIs are the IPIs', so the ISA
// numbers the PS/2 drivers ask for (1, 12) are refused.
int arch_irq_register(int irq, INTERRUPT_CALLBACK_FUNCTION handler)
{
    if (irq < GIC_INTID_PPI)
        return -EINVARG;
    return irq_register((uint32_t)irq, handler) < 0 ? -EINVARG : 0;
}

void arch_irq_unmask(int irq)
{
    if (irq >= GIC_INTID_PPI)
        gic_enable((uint32_t)irq);
}

void arch_irq_mask(int irq)
{
    if (irq >= GIC_INTID_PPI)
        gic_disable((uint32_t)irq);
}

// `gsi` is the SPI's INTID (virtio_bus.c: the slot's device tree
// interrupt); the slot only bounds the count, as x86's vector range.
int arch_irq_attach_gsi(uint32_t gsi, uint16_t flags, unsigned slot, bool masked,
                        INTERRUPT_CALLBACK_FUNCTION handler)
{
    if (!gic_ok || gsi < GIC_INTID_SPI || slot > 15 || irq_register_late(gsi, handler) < 0)
        return -EINVARG;
    gic_disable(gsi);
    gic_set_priority(gsi, PRIO_COMPLETION);
    gic_set_edge(gsi, (flags & ACPI_INTI_TRIGGER_MASK) == ACPI_INTI_TRIGGER_EDGE);
    gic_route(gsi, this_cpu()->arch.mpidr);
    if (!masked)
        gic_enable(gsi);
    return (int)gsi;
}

int arch_irq_set_cpu(uint32_t gsi, struct cpu* c)
{
    if (!gic_ok || gsi < GIC_INTID_SPI)
        return -EINVARG;
    gic_route(gsi, c->arch.mpidr);
    return 0;
}

// ICC_PMR_EL1 is PMR_DISK_WAIT meanwhile, so only the completion lines
// are taken. The state is the old PMR and DAIF.
uint64_t arch_irq_wait_begin(void)
{
    uint64_t daif = arch_irq_save();
    uint64_t pmr = sysreg_read(ICC_PMR_EL1);
    sysreg_write(ICC_PMR_EL1, PMR_DISK_WAIT);
    return pmr << 32 | daif;
}

void arch_irq_wait_window(void)
{
    __asm__ volatile("msr daifclr, #2\n\tnop\n\tmsr daifset, #2\n\tyield" ::: "memory");
}

void arch_irq_wait_end(uint64_t state)
{
    sysreg_write(ICC_PMR_EL1, state >> 32);
    arch_irq_restore((uint32_t)state);
}

// ------------------------------------------------------------------
// Timer and time: the generic timer (timer.c)
// ------------------------------------------------------------------

// Every CPU has its own EL1 physical timer, so true (SMP needs that;
// each other CPU starts its own, timer_start_cpu).
bool arch_timer_start(uint32_t hz)
{
    timer_start(hz, task_clock);
    return true;
}

uint64_t arch_counter(void)
{
    return counter_read();
}

uint64_t arch_counter_hz(void)
{
    return counter_hz();
}

static TIME_MICROSECONDS counter_us(uint64_t c)
{
    uint64_t hz = counter_hz();
    return hz ? (c / hz) * 1000000ull + (c % hz) * 1000000ull / hz : 0;
}

TIME_MICROSECONDS arch_time_us(void)
{
    return counter_us(counter_read());
}

TIME_MICROSECONDS arch_uptime_us(void)
{
    return counter_us(counter_read() - boot_counter);
}

void udelay(TIME_MICROSECONDS microseconds)
{
    TIME_MICROSECONDS end = arch_time_us() + microseconds;
    while (arch_time_us() < end)
        cpu_relax();
}

// ------------------------------------------------------------------
// CPUs and IPIs: PSCI CPU_ON, IPIs are SGIs, the panic stop a
// Group 0 SGI (an FIQ)
// ------------------------------------------------------------------
//
// arch_start_cpus (boot CPU, from kernel_main once everything exists)
// walks the device tree's /cpus (QEMU's, or acpi_dt.c's from the MADT:
// the same shape) and starts each other "psci" CPU with CPU_ON at
// aarch64_secondary_entry (boot.S), x0 = its start record. One at a
// time: the record names its struct cpu and a fresh kernel stack;
// boot.S drops to EL1, turns the MMU on with the kernel's tables and
// mmu_secondary_prepare's identity map of the text, and calls
// aarch64_secondary_main, which sets TPIDR_EL1, PAN, the kernel's
// TTBR0, its redistributor and CPU interface, its own timer, and
// enters the scheduler.
//
// The start handshake lives in the record, not in the slot: a CPU that
// answers after the boot CPU gave up on it finds GAVE_UP, says PARKED
// and turns itself off (PSCI CPU_OFF), so it never touches the slot
// the next CPU gets (x86 holds such an AP with INIT instead).

#define PSCI_CPU_OFF      0x84000002
#define PSCI_CPU_ON       0xc4000003
#define SGI_STOP          15            // Group 0: the panic stop
#define AP_STACK          0x10000       // as the boot stack (linker.ld)
#define AP_START_WINDOW_US 5000000
#define AP_WAIT           0
#define AP_CLAIMED        1
#define AP_GAVE_UP        2
#define AP_PARKED         3

// boot.S reads stack_top at offset 8.
struct cpu_start {
    struct cpu*       cpu;
    uint64_t          stack_top;
    volatile uint32_t state;
};
_Static_assert(__builtin_offsetof(struct cpu_start, stack_top) == 8, "boot.S cpu_start layout");
// vectors.S CPU_STACK_BOTTOM, CPU_KERNEL_STACK
_Static_assert(__builtin_offsetof(struct cpu, arch.stack_bottom) == 496, "vectors.S struct cpu offsets");
_Static_assert(__builtin_offsetof(struct cpu, arch.kernel_stack) == 504, "vectors.S struct cpu offsets");

extern char aarch64_secondary_entry[];
void aarch64_secondary_main(struct cpu_start* s) __attribute__((noreturn));

static int fiq_stop;                // the panic stop SGI is Group 0 (gic_fiq_sgi)

static int64_t psci_call3(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3)
{
    register uint64_t x0 __asm__("x0") = fn;
    register uint64_t x1 __asm__("x1") = a1;
    register uint64_t x2 __asm__("x2") = a2;
    register uint64_t x3 __asm__("x3") = a3;
    if (psci_smc)
        __asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) :: "memory");
    else
        __asm__ volatile("hvc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) :: "memory");
    return (int64_t)x0;
}

static void pan_on(void)
{
    if ((sysreg_read("id_aa64mmfr1_el1") >> ID_PAN_SHIFT) & 0xf) {
        __asm__ volatile(".inst 0xd500419f" ::: "memory");     // msr pan, #1
        isb();
    }
}

// This CPU's interrupts: SGIs (IPIs), the stop SGI as an FIQ, FIQs
// unmasked for good (only the stop uses Group 0).
static void cpu_irqs_init(void)
{
    for (uint32_t i = 0; i < IPI_SGIS; i++)
        gic_enable(i);
    fiq_stop = gic_fiq_sgi(SGI_STOP);
    __asm__ volatile("msr daifclr, #1" ::: "memory");
}

void aarch64_secondary_main(struct cpu_start* s)
{
    struct cpu* c = s->cpu;
    if (!atomic_cas(&s->state, AP_WAIT, AP_CLAIMED)) {
        atomic_store(&s->state, AP_PARKED);
        psci_call3(PSCI_CPU_OFF, 0, 0, 0);
        aarch64_halt();
    }
    sysreg_write("tpidr_el1", (uint64_t)c);
    pan_on();
    // Off the identity map, and no global entry of it left in this
    // CPU's TLB (the boot CPU's mmu_late does the same).
    paging_switch(kernel_desc());
    arch_tlb_flush_local(0);
    if (gic_init_cpu() < 0) {
        console_printf("smp: cpu %u: no GIC system register interface, halted\n", c->index);
        aarch64_halt();
    }
    cpu_irqs_init();
    timer_start_cpu();

    c->online = 1;
    atomic_inc(&cpu_online_count);
    console_printf("smp: cpu %u mpidr 0x%lx online\n", c->index, c->arch.mpidr);
    task_next();
    aarch64_halt();
}

static bool cpu_outstanding;        // given up on, not seen to park

static bool wait_state(struct cpu_start* s, uint32_t want, uint64_t us)
{
    uint64_t end = arch_time_us() + us;
    while (s->state != want && arch_time_us() < end)
        cpu_relax();
    return s->state == want;
}

// Start the CPU with this MPIDR in slot `index`. True when it came up.
static bool start_cpu(int index, uint64_t mpidr, bool give_up_now)
{
    struct cpu* c = &cpus[index];
    struct cpu_start* s = kzalloc(sizeof(*s));
    uint8_t* stack = kzalloc(AP_STACK);
    uint64_t gicr = gic_redistributor(mpidr);
    if (!s || !stack || !gicr) {
        console_printf("smp: mpidr 0x%lx: %s, not started\n", mpidr,
                       gicr ? "no memory for its stack" : "no GIC redistributor");
        kfree(s);
        kfree(stack);
        return false;
    }
    memset(c, 0, sizeof(*c));
    c->self = c;
    c->index = (uint32_t)index;
    c->hw_id = (uint32_t)mpidr;
    c->arch.mpidr = mpidr;
    c->arch.gicr = gicr;
    c->arch.stack_bottom = stack;
    c->arch.kernel_stack = stack + AP_STACK;
    s->cpu = c;
    s->stack_top = (uint64_t)c->arch.kernel_stack;
    s->state = give_up_now ? AP_GAVE_UP : AP_WAIT;
    smp_mb();

    int64_t r = psci_call3(PSCI_CPU_ON, mpidr, kimage_pa(aarch64_secondary_entry), (uint64_t)s);
    if (r != 0) {
        console_printf("smp: mpidr 0x%lx: PSCI CPU_ON returned %ld\n", mpidr, r);
        kfree(s);           // nothing runs on them
        kfree(stack);
        return false;
    }
    uint64_t end = arch_time_us() + (give_up_now ? 0 : AP_START_WINDOW_US);
    while (!c->online && arch_time_us() < end)
        cpu_relax();
    if (c->online)
        return true;
    // Too late: whoever wins the record decides. A CPU that claimed it
    // is on its way online.
    if (!atomic_cas(&s->state, AP_WAIT, AP_GAVE_UP) && s->state == AP_CLAIMED) {
        while (!c->online && arch_time_us() < end + AP_START_WINDOW_US)
            cpu_relax();
        if (c->online)
            return true;
        panic("smp: a CPU claimed its slot but never came online\n");
    }
    console_printf("smp: mpidr 0x%lx did not start%s\n", mpidr, give_up_now ? " (samos.smp_late_ap)" : "");
    if (wait_state(s, AP_PARKED, give_up_now ? AP_START_WINDOW_US : 0))
        console_printf("smp: mpidr 0x%lx answered late and parked\n", mpidr);
    else
        cpu_outstanding = true;
    // s and the stack stay allocated: a late CPU may still read them.
    return false;
}

int arch_start_cpus(void)
{
    int cpus_node = dt_ok ? fdt_path_offset(&dt, "/cpus") : -1;
    if (!gic_ok || cpus_node < 0 || mmu_secondary_prepare() < 0)
        return 1;
    bool late_test = boot_cmdline_has("samos.smp_late_ap");
    int next = 1;
    for (int n = fdt_first_child(&dt, cpus_node); n >= 0 && next < SAMOS_MAX_CPUS; n = fdt_next_sibling(&dt, n)) {
        const char* type = fdt_getprop_string(&dt, n, "device_type");
        const char* method = fdt_getprop_string(&dt, n, "enable-method");
        const char* status = fdt_getprop_string(&dt, n, "status");
        struct fdt_region reg;
        if (!type || strncmp(type, "cpu", 4) || (status && strncmp(status, "okay", 5)) || fdt_reg(&dt, n, 0, &reg) < 0)
            continue;
        uint64_t mpidr = reg.base & 0xff00ffffffull;
        if (mpidr == cpus[0].arch.mpidr)
            continue;
        if (!method || strncmp(method, "psci", 5)) {
            console_printf("smp: mpidr 0x%lx: enable-method %s, not started\n", mpidr, method ? method : "none");
            continue;
        }
        if (start_cpu(next, mpidr, late_test))
            next++;
        late_test = false;
    }
    // A CPU given up on that has not parked may still turn its MMU on
    // with the identity map: keep it then.
    if (!cpu_outstanding)
        mmu_secondary_done();
    print("smp: ");
    print(itoa((int)cpu_online_count));
    print(" cpus online\n");
    boot_marker_set(BM_STAGE_SMP_CPUS, cpu_online_count);
    return (int)cpu_online_count;
}

void arch_send_ipi(struct cpu* target, enum arch_ipi ipi)
{
    if (gic_ok && (unsigned)ipi < IPI_SGIS)
        gic_send_sgi((uint32_t)ipi, target->arch.mpidr);
}

// x86's NMI: an FIQ reaches a CPU spinning with IRQs masked.
bool arch_stop_other_cpus(void)
{
    if (!gic_ok || !fiq_stop)
        return false;
    gic_send_fiq_others(SGI_STOP);
    return true;
}

// ------------------------------------------------------------------
// Firmware: the device tree. The initrd becomes the RAM disk, the
// virtio-mmio slots go to virtio_bus.c, fw_cfg gives the ramfb screen.
// ------------------------------------------------------------------

// A virtio-mmio node's SPI, and its trigger as MPS INTI flags (device
// tree flags 1 and 2 are edges, 4 and 8 levels).
static int dt_virtio_irq(const struct fdt* f, int node, uint16_t* inti)
{
    uint32_t flags = gic_dt_flags(f, node, 0);
    *inti = ACPI_INTI_POLARITY_HIGH | ((flags & 3) ? ACPI_INTI_TRIGGER_EDGE : ACPI_INTI_TRIGGER_LEVEL);
    return gic_dt_intid(f, node, 0);
}

static void ramfb_init(void)
{
    int node = fdt_find_compatible(&dt, -1, "qemu,fw-cfg-mmio");
    struct fdt_region reg;
    if (node < 0 || fdt_reg_cpu(&dt, node, 0, &reg) < 0) {
        serial_write("ramfb: no fw_cfg in the device tree, no screen\n");
        return;
    }
    void* fw = paging_map_mmio(reg.base, reg.size, PAGING_CACHE_DISABLED);
    void* fb = pfa_alloc_frames(FB_BYTES / PAGE_SIZE);
    if (!fw || !fb) {
        serial_write("ramfb: no memory for the framebuffer, no screen\n");
        return;
    }
    uint64_t fb_pa = virt_to_phys(fb);
    for (uint32_t i = 0; i < FB_WIDTH * FB_HEIGHT; i++)
        ((uint32_t*)fb)[i] = 0x0000ff00;        // green, as SamOs.c leaves the GOP
    int r =ramfb_setup(fw, &ramfb_dma, kimage_pa(&ramfb_dma), fb_pa, FB_WIDTH, FB_HEIGHT);
    if (r < 0) {
        pfa_free_frames(fb);
        console_printf("ramfb: not available (%d, -device ramfb?), no screen\n", r);
        return;
    }
    boot_info.fb_base = fb_pa;
    boot_info.fb_size = FB_BYTES;
    boot_info.fb_width = FB_WIDTH;
    boot_info.fb_height = FB_HEIGHT;
    boot_info.fb_pitch = FB_WIDTH;
    boot_info.fb_format = SAMOS_FB_FORMAT_BGRX;
    default_graphics_info.framebuffer = (struct framebuffer_pixel*)fb_pa;
    console_printf("ramfb: %ux%u XRGB8888 framebuffer at pa 0x%lx\n", FB_WIDTH, FB_HEIGHT, fb_pa);
}

// The PCIe host's ECAM window ("pci-host-ecam-generic", QEMU virt),
// mapped whole for io/pci.c. Enumeration only: nothing assigned the
// BARs, and pci_intx_gsi knows no route on this host bridge.
static void pci_ecam_dt_init(void)
{
    int node = fdt_find_compatible(&dt, -1, "pci-host-ecam-generic");
    struct fdt_region reg;
    if (node < 0 || fdt_reg_cpu(&dt, node, 0, &reg) < 0)
        return;
    uint32_t len;
    const uint8_t* range = fdt_getprop(&dt, node, "bus-range", &len);
    uint32_t first = 0, last = (uint32_t)(reg.size >> 20) - 1;
    if (range && len == 8) {
        first = (uint32_t)range[0] << 24 | range[1] << 16 | range[2] << 8 | range[3];
        last = (uint32_t)range[4] << 24 | range[5] << 16 | range[6] << 8 | range[7];
    }
    if (last > 255 || first > last || ((uint64_t)(last - first + 1) << 20) > reg.size)
        return;
    void* v = paging_map_mmio(reg.base, reg.size, PAGING_CACHE_DISABLED);
    if (v)
        pci_ecam_install_range(0, reg.base, (uint8_t)first, (uint8_t)last, v);
}

// The PL031 real-time clock: RTCDR counts seconds since 1970.
static volatile uint32_t* pl031;

static int64_t pl031_unix_time(void)
{
    return pl031[0];
}

static void pl031_init(void)
{
    int node = fdt_find_compatible(&dt, -1, "arm,pl031");
    struct fdt_region reg;
    if (node < 0 || fdt_reg_cpu(&dt, node, 0, &reg) < 0)
        return;
    pl031 = paging_map_mmio(reg.base, reg.size, PAGING_CACHE_DISABLED);
    if (pl031)
        rtc_set_unix_source(pl031_unix_time);
}

void arch_firmware_init(void)
{
    pci_ecam_dt_init();
    pl031_init();
    int slots = virtio_mmio_scan_fdt(&dt, dt_virtio_irq, true);
    console_printf("virtio-mmio: %d slot(s) in the device tree\n", slots);
    ramfb_init();

    uint64_t base, size;
    if (memmap_initrd(&base, &size) == 0) {
        serial_write("initrd: ");
        serial_write_hex(base);
        serial_write(", ");
        serial_write(itoa((int)(size / 1024)));
        serial_write(" KiB, the RAM disk\n");
        ramdisk_attach(base, size);
    } else {
        serial_write("initrd: none (-initrd), no disk\n");
    }
}

uint8_t  arch_pio_read8(uint16_t port)  { return 0xff; }
uint16_t arch_pio_read16(uint16_t port) { return 0xffff; }
uint32_t arch_pio_read32(uint16_t port) { return 0xffffffff; }
void arch_pio_write8(uint16_t port, uint8_t v)   {}
void arch_pio_write16(uint16_t port, uint16_t v) {}
void arch_pio_write32(uint16_t port, uint32_t v) {}

uint32_t arch_pci_cfg_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off)
{
    return 0xffffffff;
}

void arch_pci_cfg_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off, uint32_t v)
{
}

// ------------------------------------------------------------------
// Power: PSCI, by the conduit the device tree names
// ------------------------------------------------------------------

static void psci_call(uint64_t fn)
{
    register uint64_t x0 __asm__("x0") = fn;
    if (psci_smc)
        __asm__ volatile("smc #0" : "+r"(x0) :: "x1", "x2", "x3", "memory");
    else
        __asm__ volatile("hvc #0" : "+r"(x0) :: "x1", "x2", "x3", "memory");
}

void arch_shutdown_fallback(void)
{
    serial_write("psci: SYSTEM_OFF\n");
    psci_call(PSCI_SYSTEM_OFF);
}

void arch_reboot_fallback(void)
{
    serial_write("psci: SYSTEM_RESET\n");
    psci_call(PSCI_SYSTEM_RESET);
}
