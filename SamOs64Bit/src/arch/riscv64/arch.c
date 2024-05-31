// riscv64's side of arch/arch.h that is not in a file of its
// own (paging.c, fpu.S, trap.c, uaccess.c): boot hooks, the console,
// the return to U-mode, the PLIC and the timer behind the arch_irq_*
// and arch_timer_* calls, time, the firmware (device tree) and the
// things riscv64 does not have (port I/O, PCI mechanism 1). The
// other harts are smp.c's.

#include "arch/arch.h"
#include "arch/riscv64/console.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/aia.h"
#include "arch/riscv64/plic.h"
#include "arch/riscv64/riscv64.h"
#include "arch/riscv64/sbi.h"
#include "arch/riscv64/timer.h"
#include "boot/boot_info.h"
#include "cpu/percpu.h"
#include "acpi/acpi.h"
#include "arch/riscv64/uart16550.h"
#include "disk/drivers/ramdisk.h"
#include "firmware/fdt.h"
#include "firmware/ramfb.h"
#include "io/pci_fdt.h"
#include "graphics/graphics.h"
#include "kernel.h"
#include "keyboard/serial_input.h"
#include "virtio/virtio_mmio_fdt.h"
#include "memory/pfa.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "string/string.h"
#include "status.h"

// The ramfb mode.
#define RAMFB_WIDTH     1024
#define RAMFB_HEIGHT    768

// Set by riscv64_main before kernel_main (main.c).
struct samos_boot_info* samos_boot_info_ptr;
// ramfb's, when QEMU has one (arch_firmware_init); else all zero
// and graphics runs headless.
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
    if (riscv64_trap_stack(this_cpu()) != 0) {
        panic("riscv64: no trap stack\n");
    }
}

// The boot hart's (arch_trap_init) and each other hart's
// (arch_start_cpus, smp.c).
int riscv64_trap_stack(struct cpu* c)
{
    uint8_t* stack = kzalloc(RISCV64_TRAP_STACK_SIZE + 16);
    if (!stack) {
        return -ENOMEM;
    }
    c->arch.kernel_stack = stack + RISCV64_TRAP_STACK_SIZE;
    *(struct cpu**)c->arch.kernel_stack = c;
    return 0;
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

// The console UART's receive interrupt: every byte goes to the
// keyboard layer (keyboard/serial_input.c).
static void uart_rx_interrupt(struct trap_frame* frame)
{
    int c;
    while ((c = uart16550_getc()) >= 0) {
        serial_input_rx((char)c);
    }
}

// Parity - or, without a PLIC (virt,aia=aplic-imsic), APLIC sources
// delivered as messages to the IMSIC (aia.h).
static bool aia_up;

static int ext_register(unsigned irq, INTERRUPT_CALLBACK_FUNCTION handler)
{
    return aia_up ? aia_register_handler(irq, handler) : plic_register_handler(irq, handler);
}

static void ext_enable(unsigned irq)
{
    if (aia_up) {
        aia_enable(irq);
    } else {
        plic_enable(irq);
    }
}

// And the software interrupt (the IPI, arch_send_ipi), and the
// UART's receive line as a keyboard.
void arch_irq_init(void)
{
    const struct fdt* f = riscv64_fdt();
    int cpu = riscv64_cpu_node(f, this_cpu()->arch.hart_id);
    plic_up = plic_init(f, cpu) == 0;
    aia_up = !plic_up && aia_init(f, cpu) == 0;
    csr_set(sie, SIE_SSIE);
    int uart_irq = riscv64_uart_irq();
    if ((plic_up || aia_up) && uart_irq > 0 && ext_register(uart_irq, uart_rx_interrupt) == 0) {
        serial_input_init();
        ext_enable(uart_irq);
        uart16550_enable_rx_irq();
        console_printf("uart: keys from %s irq %d\n", aia_up ? "aplic" : "plic", uart_irq);
    }
}

// Device lines (arch_irq_attach_gsi) are PLIC sources.
bool arch_irq_routable(void)
{
    return plic_up || aia_up;
}

// The shared callers' IRQ numbers are x86 ISA lines (IRQ_KEYBOARD,
// IRQ_PS2_MOUSE); this board has none, and the same numbers here are
// PLIC sources of other devices (1-8: the virtio-mmio slots). Device
// lines come through arch_irq_attach_gsi.
int arch_irq_register(int irq, INTERRUPT_CALLBACK_FUNCTION handler)
{
    return -EIO;
}

void arch_irq_unmask(int irq)
{
}

void arch_irq_mask(int irq)
{
}

// A device-tree interrupt is a PLIC source; the slot only names the
// driver's line on x86.
int arch_irq_attach_gsi(uint32_t gsi, uint16_t flags, unsigned slot, bool masked,
                        INTERRUPT_CALLBACK_FUNCTION handler)
{
    if (!arch_irq_routable() || ext_register(gsi, handler) != 0) {
        return -EINVARG;
    }
    if (!masked) {
        ext_enable(gsi);
    }
    return (int)gsi;
}

// A disk wait steers its line to the waiting hart's context.
int arch_irq_set_cpu(uint32_t gsi, struct cpu* c)
{
    return aia_up ? aia_route(gsi, c) : plic_route(gsi, c->arch.plic_ctx);
}

// Parity - PCI MSI and MSI-X need the IMSIC; with the PLIC the xHCI
// driver takes INTx.
int arch_irq_msi(struct pci_device* dev, unsigned slot, struct cpu* c, INTERRUPT_CALLBACK_FUNCTION handler,
                 uint64_t* addr, uint32_t* data)
{
    return aia_up ? aia_msi(slot, c, handler, addr, data) : -EUNIMP;
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

// Every hart has its own tick (timer_start_hart), so the
// kernel may start the others.
bool arch_timer_start(uint32_t hz)
{
    const struct fdt* f = riscv64_fdt();
    if (timer_init(f, riscv64_cpu_node(f, this_cpu()->arch.hart_id), hz) != 0) {
        panic("riscv64: no timer\n");
    }
    return true;
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
// CPUs: the riscv64 SMP work starts the other harts, stops them on a panic and halts
// (smp.c).
// ------------------------------------------------------------------

// The kind goes into the target's ipi_pending, then its
// sip.SSIP: this hart sets its own (virtio-input and the UART hand
// their keys over this way), another through the SBI. A full fence (amoor.aqrl), so the kind is visible before OpenSBI's
// write that raises the target's interrupt.
void arch_send_ipi(struct cpu* target, enum arch_ipi ipi)
{
    __atomic_fetch_or(&target->arch.ipi_pending, 1u << ipi, __ATOMIC_SEQ_CST);
    if (target == this_cpu()) {
        csr_set(sip, SIE_SSIE);
    } else {
        sbi_send_ipi(1, target->arch.hart_id);
    }
}

// ------------------------------------------------------------------
// Firmware, port I/O, PCI configuration space, power
// ------------------------------------------------------------------

// A virtio-mmio node's PLIC source, the first `interrupts` cell (the
// APLIC's specifier adds the trigger type), raised level-triggered.
static int virtio_plic_source(const struct fdt* f, int node, uint16_t* inti)
{
    *inti = ACPI_INTI_TRIGGER_LEVEL | ACPI_INTI_POLARITY_HIGH;
    return riscv64_dt_irq(f, node);
}

// Parity - the PCIe host bridge is io/pci_fdt.c's (ECAM, BARs from its
// ranges); a pin's interrupt-map entry names a PLIC source (one cell)
// or, with aia=aplic-imsic, an APLIC source and its trigger type.
// QEMU raises INTx level, active high, on either.
static int pci_intx_source(const struct fdt* f, int parent, const uint32_t* cell, uint32_t n, uint16_t* inti)
{
    if (n < 1 || cell[0] == 0 || !(fdt_node_is_compatible(f, parent, "riscv,aplic")
        || fdt_node_is_compatible(f, parent, "sifive,plic-1.0.0") || fdt_node_is_compatible(f, parent, "riscv,plic0"))) {
        return -ENOENT;
    }
    *inti = ACPI_INTI_TRIGGER_LEVEL | ACPI_INTI_POLARITY_HIGH;
    return (int)cell[0];
}

// ramfb (firmware/ramfb.h) as the screen when QEMU has one
// (-device ramfb): frames from the frame allocator, a page for the
// fw_cfg DMA request, the result in boot_info's fb_* and
// default_graphics_info. 0, or < 0 with nothing kept.
static int ramfb_init(const struct fdt* f)
{
    struct fdt_region reg;
    int node = fdt_find_compatible(f, -1, "qemu,fw-cfg-mmio");
    if (node < 0 || fdt_reg_cpu(f, node, 0, &reg) != 0) {
        return -ENOENT;
    }
    volatile void* fw_cfg = paging_map_mmio(reg.base, reg.size, PAGING_CACHE_DISABLED);
    uint64_t size = (uint64_t)RAMFB_WIDTH * RAMFB_HEIGHT * 4;
    void* frames = pfa_alloc_frames((size + 4095) / 4096);
    struct ramfb_dma* dma = kpage_zalloc(4096);
    int res = -ENOMEM;
    if (fw_cfg && frames && dma) {
        memset(frames, 0, size);      // in the direct map; black before QEMU shows it
        res = ramfb_setup(fw_cfg, dma, paging_kernel_virt_to_phys(dma), paging_kernel_virt_to_phys(frames),
                          RAMFB_WIDTH, RAMFB_HEIGHT);
    }
    kfree(dma);
    if (res < 0) {
        if (frames) {
            pfa_free_frames(frames);
        }
        return res;
    }
    struct samos_boot_info* bi = samos_boot_info_ptr;
    bi->fb_base   = paging_kernel_virt_to_phys(frames);
    bi->fb_size   = size;
    bi->fb_width  = RAMFB_WIDTH;
    bi->fb_height = RAMFB_HEIGHT;
    bi->fb_pitch  = RAMFB_WIDTH;
    bi->fb_format = SAMOS_FB_FORMAT_BGRX;
    default_graphics_info.framebuffer = (void*)bi->fb_base;
    default_graphics_info.horizontal_resolution = RAMFB_WIDTH;
    default_graphics_info.vertical_resolution = RAMFB_HEIGHT;
    default_graphics_info.pixels_per_scanline = RAMFB_WIDTH;
    return 0;
}

// The device tree is parsed already (riscv64_main); what the shared
// kernel needs from it before the disks is the initrd, its RAM disk.
// And the virtio-mmio slots (the drivers probe them later), and
// the screen: graphics_setup comes next and reads boot_info's fb_* and
// default_graphics_info.
void arch_firmware_init(void)
{
    const struct fdt* f = riscv64_fdt();
    uint64_t base, size;
    if (mmu_initrd(&base, &size) == 0) {
        console_printf("initrd: 0x%lx, %lu KiB\n", base, size >> 10);
        ramdisk_attach(base, size);
    }
    pci_fdt_host_init(f, pci_intx_source);
    // riscv64 virt plugs the first -device into the highest slot, as
    // arm virt does (virtio_mmio_fdt.h).
    int slots = virtio_mmio_scan_fdt(f, virtio_plic_source, true);
    console_printf("virtio: %d mmio slot(s) in the device tree\n", slots);
    int res = ramfb_init(f);
    if (res == 0) {
        console_printf("ramfb: %ux%u at 0x%lx\n", RAMFB_WIDTH, RAMFB_HEIGHT, samos_boot_info_ptr->fb_base);
    } else {
        console_printf("ramfb: none (%d), no screen\n", res);
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

// Without Zbb gcc turns __builtin_bswap32/64 (firmware/ramfb.c)
// into libgcc calls, and the cross toolchain's libgcc is lp64d, which
// does not link into this lp64 kernel; these come first.
uint32_t __bswapsi2(uint32_t v)
{
    return v >> 24 | (v >> 8 & 0xFF00) | (v << 8 & 0xFF0000) | v << 24;
}

uint64_t __bswapdi2(uint64_t v)
{
    return (uint64_t)__bswapsi2((uint32_t)v) << 32 | __bswapsi2((uint32_t)(v >> 32));
}
