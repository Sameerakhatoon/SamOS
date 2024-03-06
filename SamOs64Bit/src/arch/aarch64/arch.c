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
// One CPU (the aarch64 SMP work starts the others): no IPIs, no device interrupt lines
// yet, no port I/O and no legacy PCI configuration mechanism.

#include "arch/arch.h"
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
#include "bootmarker.h"
#include "cpu/percpu.h"
#include "disk/drivers/ramdisk.h"
#include "firmware/fdt.h"
#include "graphics/graphics.h"
#include "io/serial.h"
#include "kernel.h"
#include "memory/memory.h"
#include "string/string.h"
#include "status.h"
#include "task/task.h"

#define E820_MAX        64
#define EARLY_RUNS      32
#define PSCI_SYSTEM_OFF   0x84000008
#define PSCI_SYSTEM_RESET 0x84000009
#define ID_PAN_SHIFT    20          // ID_AA64MMFR1_EL1.PAN
#define SCTLR_SPAN      (1ul << 23) // 0: PSTATE.PAN set on every exception to EL1

// No screen until the aarch64 device work (ramfb or virtio-gpu): a framebuffer format the
// graphics code refuses, so it draws into RAM only.
#define FB_FORMAT_NONE  2
#define FB_WIDTH        640
#define FB_HEIGHT       480

struct samos_boot_info* samos_boot_info_ptr;
struct graphics_info default_graphics_info;
uint64_t aarch64_boot_markers[BM_STAGE_MAX] __attribute__((aligned(4096)));

extern char _head[], _end[], _stack_top[];
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
    c->arch.kernel_stack = (uint8_t*)_stack_top;
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

void arch_irq_init(void)
{
    if (!gic_ok && dt_ok)
        gic_ok = gic_init(&dt) == 0;
}

void aarch64_gic_ready(void)
{
    gic_ok = 1;
}

bool arch_irq_routable(void)
{
    return false;       // Device lines through arch_irq_attach_gsi
}

int arch_irq_register(int irq, INTERRUPT_CALLBACK_FUNCTION handler)
{
    return irq_register((uint32_t)irq, handler) < 0 ? -EINVARG : 0;
}

void arch_irq_unmask(int irq)
{
    gic_enable((uint32_t)irq);
}

void arch_irq_mask(int irq)
{
    gic_disable((uint32_t)irq);
}

int arch_irq_attach_gsi(uint32_t gsi, uint16_t flags, unsigned slot, bool masked,
                        INTERRUPT_CALLBACK_FUNCTION handler)
{
    return -EINVARG;
}

int arch_irq_set_cpu(uint32_t gsi, struct cpu* c)
{
    return -EINVARG;
}

// No completion lines yet: the window just opens IRQs for a moment.
uint64_t arch_irq_wait_begin(void)
{
    return arch_irq_save();
}

void arch_irq_wait_window(void)
{
    __asm__ volatile("msr daifclr, #2\n\tnop\n\tmsr daifset, #2" ::: "memory");
}

void arch_irq_wait_end(uint64_t state)
{
    arch_irq_restore(state);
}

// ------------------------------------------------------------------
// Timer and time: the generic timer (timer.c)
// ------------------------------------------------------------------

// Every CPU has its own EL1 physical timer, so true (SMP needs that;
// arch_start_cpus has no other CPU to start before the aarch64 SMP work).
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
// CPUs and IPIs: one CPU until the aarch64 SMP work (PSCI CPU_ON, SGIs)
// ------------------------------------------------------------------

int arch_start_cpus(void)
{
    return 1;
}

void arch_send_ipi(struct cpu* target, enum arch_ipi ipi)
{
}

bool arch_stop_other_cpus(void)
{
    return false;
}

// ------------------------------------------------------------------
// Firmware: the device tree. The initrd becomes the RAM disk.
// ------------------------------------------------------------------

void arch_firmware_init(void)
{
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
