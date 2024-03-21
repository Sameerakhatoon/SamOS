// riscv64's side of arch/arch.h that is not in a file of its
// own (paging.c, fpu.S, trap.c, uaccess.c): boot hooks, the console,
// the return to U-mode, the PLIC and the timer behind the arch_irq_*
// and arch_timer_* calls, time, the firmware (device tree) and the
// things riscv64 does not have (port I/O, PCI mechanism 1, SMP until
// The riscv64 SMP work).

#include "arch/arch.h"
#include "arch/riscv64/console.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/plic.h"
#include "arch/riscv64/riscv64.h"
#include "arch/riscv64/sbi.h"
#include "arch/riscv64/timer.h"
#include "boot/boot_info.h"
#include "cpu/percpu.h"
#include "disk/drivers/ramdisk.h"
#include "firmware/fdt.h"
#include "graphics/graphics.h"
#include "kernel.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "string/string.h"
#include "status.h"

#define TRAP_STACK_SIZE (16 * 1024)

// Set by riscv64_main before kernel_main (main.c).
struct samos_boot_info* samos_boot_info_ptr;
// No framebuffer until the riscv64 device work (ramfb): all zero, graphics runs headless.
struct graphics_info default_graphics_info;
// The e2e boot markers (bootmarker.h): RAM does not start at 0 here.
volatile uint64_t riscv64_boot_markers[256];

static uint64_t counter_hz, counter_start;

// trap_entry's second half (boot.S): restore `frame` and sret.
void trap_return(struct trap_frame* frame) __attribute__((noreturn));

// ------------------------------------------------------------------
// Boot and CPU set-up
// ------------------------------------------------------------------

// riscv64_main did the early part (MMU, console, memory map) before
// kernel_main; here only the counter's rate, which arch_time_us needs.
void arch_early_init(void)
{
    const struct fdt* f = riscv64_fdt();
    counter_hz = timer_probe_timebase(f, riscv64_cpu_node(f, this_cpu()->arch.hart_id));
    counter_start = rdtime();
}

// stvec is trap_entry since boot.S. The trap stack for traps from
// U-mode, with this hart's struct cpu in the word at its top (boot.S
// loads tp from there).
void arch_trap_init(void)
{
    struct cpu* c = this_cpu();
    uint8_t* stack = kzalloc(TRAP_STACK_SIZE + 16);
    if (!stack) {
        panic("riscv64: no trap stack\n");
    }
    c->arch.kernel_stack = stack + TRAP_STACK_SIZE;
    *(struct cpu**)c->arch.kernel_stack = c;
}

void arch_halt(void)
{
    riscv64_halt();
}

void arch_wait_for_interrupt(void)
{
    __asm__ volatile("csrsi sstatus, 2; wfi; csrci sstatus, 2" ::: "memory");
}

// QEMU's virt machine; the device tree's root says so.
bool arch_on_hypervisor(void)
{
    const struct fdt* f = riscv64_fdt();
    return fdt_node_is_compatible(f, fdt_path_offset(f, "/"), "riscv-virtio");
}

// The SBI answered (spec version, implementation), as CPUID does.
bool arch_cpu_identified(void)
{
    return sbi_probe(SBI_EXT_BASE);
}

// No stack that a fault cannot run into yet: the bad-stack
// test has nothing to test.
void arch_fault_test(int kind)
{
    if (kind == ARCH_FAULT_TEST_UNDEFINED) {
        trap_test_illegal();
    } else {
        console_printf("arch: no separate fault stack on riscv64\n");
    }
}

// ------------------------------------------------------------------
// Console: riscv64_main moved it from the SBI to the 16550 already.
// ------------------------------------------------------------------

bool arch_console_init(void)
{
    return true;
}

void arch_console_putc(char c)
{
    console_raw_putc(c);
}

// ------------------------------------------------------------------
// Traps and the return to U-mode
// ------------------------------------------------------------------

void arch_kernel_context(void)
{
}

void arch_user_context(void)
{
}

// The frame goes to the top of this hart's trap stack, where a trap
// from U-mode would have left it, so trap_return points sscratch at
// that top again. Whatever ran on the trap stack before is done with:
// the kernel keeps no task state on it across task_next. U-mode
// with interrupts on after sret, the FP unit Clean (task_enter has just
// restored it, fpu.S).
void arch_task_return(struct trap_frame* frame)
{
    arch_irq_disable();
    struct trap_frame* top = (struct trap_frame*)this_cpu()->arch.kernel_stack - 1;
    if (top != frame) {
        memcpy(top, frame, sizeof(*top));
    }
    top->sstatus = (top->sstatus & ~(SSTATUS_SPP | SSTATUS_SIE | SSTATUS_SUM | SSTATUS_FS))
                 | SSTATUS_SPIE | SSTATUS_FS_CLEAN;
    trap_return(top);
}

// ------------------------------------------------------------------
// Interrupt controller: the PLIC, IRQ numbers its source ids.
// ------------------------------------------------------------------

static bool plic_up;

void arch_irq_init(void)
{
    const struct fdt* f = riscv64_fdt();
    plic_up = plic_init(f, riscv64_cpu_node(f, this_cpu()->arch.hart_id)) == 0;
}

// No GSI routing for PCI INTx yet.
bool arch_irq_routable(void)
{
    return false;
}

int arch_irq_register(int irq, INTERRUPT_CALLBACK_FUNCTION handler)
{
    return plic_up ? plic_register_handler(irq, handler) : -EIO;
}

void arch_irq_unmask(int irq)
{
    plic_enable(irq);
}

void arch_irq_mask(int irq)
{
    plic_disable(irq);
}

// A device-tree interrupt is a PLIC source; the slot only names the
// driver's line on x86.
int arch_irq_attach_gsi(uint32_t gsi, uint16_t flags, unsigned slot, bool masked,
                        INTERRUPT_CALLBACK_FUNCTION handler)
{
    if (!plic_up || plic_register_handler(gsi, handler) != 0) {
        return -EINVARG;
    }
    if (!masked) {
        plic_enable(gsi);
    }
    return (int)gsi;
}

int arch_irq_set_cpu(uint32_t gsi, struct cpu* c)
{
    return 0;
}

// Only external interrupts in the window: the timer and IPIs wait
// (sie), as x86 raises the TPR. Bit 63 of the state is sstatus.SIE.
uint64_t arch_irq_wait_begin(void)
{
    uint64_t sie = csr_read_clear(sie, SIE_STIE | SIE_SSIE);
    return sie | (csr_read(sstatus) & SSTATUS_SIE ? 1ul << 63 : 0);
}

void arch_irq_wait_window(void)
{
    __asm__ volatile("csrsi sstatus, 2; nop; csrci sstatus, 2" ::: "memory");
}

void arch_irq_wait_end(uint64_t state)
{
    csr_set(sie, state & (SIE_STIE | SIE_SSIE));
    if (state >> 63) {
        arch_irq_enable();
    }
}

// ------------------------------------------------------------------
// Timer and time: the riscv64 trap tick (Sstc or SBI), the time CSR.
// ------------------------------------------------------------------

// One hart until the riscv64 SMP work, so no per-CPU tick to report yet.
bool arch_timer_start(uint32_t hz)
{
    const struct fdt* f = riscv64_fdt();
    if (timer_init(f, riscv64_cpu_node(f, this_cpu()->arch.hart_id), hz) != 0) {
        panic("riscv64: no timer\n");
    }
    return false;
}

uint64_t arch_counter(void)
{
    return rdtime();
}

uint64_t arch_counter_hz(void)
{
    return counter_hz;
}

TIME_MICROSECONDS arch_time_us(void)
{
    uint64_t t = rdtime();
    return counter_hz ? t / counter_hz * 1000000 + t % counter_hz * 1000000 / counter_hz : 0;
}

TIME_MICROSECONDS arch_uptime_us(void)
{
    uint64_t t = rdtime() - counter_start;
    return counter_hz ? t / counter_hz * 1000000 + t % counter_hz * 1000000 / counter_hz : 0;
}

void udelay(TIME_MICROSECONDS microseconds)
{
    TIME_MICROSECONDS end = arch_time_us() + microseconds;
    while (arch_time_us() < end) {
        cpu_relax();
    }
}

// ------------------------------------------------------------------
// CPUs: the boot hart only until the riscv64 SMP work (SBI HSM, SBI IPIs).
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
// Firmware, port I/O, PCI configuration space, power
// ------------------------------------------------------------------

// The device tree is parsed already (riscv64_main); what the shared
// kernel needs from it before the disks is the initrd, its RAM disk.
void arch_firmware_init(void)
{
    uint64_t base, size;
    if (mmu_initrd(&base, &size) == 0) {
        console_printf("initrd: 0x%lx, %lu KiB\n", base, size >> 10);
        ramdisk_attach(base, size);
    }
}

uint8_t  arch_pio_read8(uint16_t port)  { return 0xFF; }
uint16_t arch_pio_read16(uint16_t port) { return 0xFFFF; }
uint32_t arch_pio_read32(uint16_t port) { return 0xFFFFFFFF; }
void arch_pio_write8(uint16_t port, uint8_t v)   { }
void arch_pio_write16(uint16_t port, uint16_t v) { }
void arch_pio_write32(uint16_t port, uint32_t v) { }

uint32_t arch_pci_cfg_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off)
{
    return 0xFFFFFFFF;
}

void arch_pci_cfg_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off, uint32_t v)
{
}

void arch_shutdown_fallback(void)
{
    sbi_shutdown();
}

void arch_reboot_fallback(void)
{
    sbi_reboot();
}
