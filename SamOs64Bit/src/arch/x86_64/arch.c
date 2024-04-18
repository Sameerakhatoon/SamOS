// x86-64's side of arch/arch.h that is not in a file of its
// own: CPU set-up order, the COM1 console, the interrupt controller
// and timer choice that kernel_main used to make, port I/O and PCI
// configuration space, the e2e fault hooks, power-off and reset
// fallbacks.

#include "arch/arch.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/cpuid.h"
#include "arch/x86_64/tsc.h"
#include "arch/x86_64/pit.h"
#include "arch/x86_64/irq.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/apic.h"
#include "arch/x86_64/smp.h"
#include "boot/cmdline.h"
#include "boot/boot_info.h"
#include "acpi/acpi.h"
#include "cpu/percpu.h"
#include "io/serial.h"
#include "status.h"

// ------------------------------------------------------------------
// Boot and CPU set-up
// ------------------------------------------------------------------

// Check the CPU and set CR0, CR4 and EFER before any code that
// depends on them. TSC frequency, checked against PIT
// channel 2.
void arch_early_init(void){
    cpu_init();
    tsc_init();
}

// The TSS first, since the #DF, NMI and #MC gates name its IST
// stacks. The TSS and stacks are per CPU.
void arch_trap_init(void){
    smp_bsp_init();
    idt_init();
}

void arch_halt(void){
    while(1){
        __asm__ volatile("cli; hlt");
    }
}

void arch_wait_for_interrupt(void){
    __asm__ volatile("sti; hlt; cli" ::: "memory");
}

// CPUID.1:ECX bit 31, set by every hypervisor.
bool arch_on_hypervisor(void){
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    return (ecx >> 31) & 1;
}

// CPUID leaf 0 returns the vendor signature in ebx/edx/ecx; on any
// real x86 (or KVM/TCG) one of those is non-zero.
bool arch_cpu_identified(void){
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    return (ebx | ecx | edx) != 0;
}

// e2e hooks: ud2, and a push onto an unmapped stack, which
// cannot deliver #PF and so must reach #DF on its IST stack.
void arch_fault_test(int kind){
    if(kind == ARCH_FAULT_TEST_UNDEFINED){
        __asm__ volatile("ud2");
    } else if(kind == ARCH_FAULT_TEST_BAD_STACK){
        __asm__ volatile("mov %0, %%rsp; push %%rax" : : "r"(0x00007ff000000000ull));
    }
}

// ------------------------------------------------------------------
// Early console: COM1. Register offsets from the 16550
// datasheet: +0 data / divisor-low (DLAB=1), +1 IER / divisor-high,
// +2 FIFO control, +3 line control, +4 modem control, +5 line status.
// ------------------------------------------------------------------

#define COM1      0x3F8
#define COM_DATA  0
#define COM_IER   1
#define COM_FCR   2
#define COM_LCR   3
#define COM_MCR   4
#define COM_LSR   5
#define COM_LSR_THR_EMPTY 0x20

bool arch_console_init(void){
    outb(COM1 + COM_IER, 0x00);   // no UART interrupts, we poll
    outb(COM1 + COM_LCR, 0x80);   // DLAB on
    outb(COM1 + COM_DATA, 0x01);  // divisor 1 = 115200 baud
    outb(COM1 + COM_IER, 0x00);
    outb(COM1 + COM_LCR, 0x03);   // 8N1, DLAB off
    outb(COM1 + COM_FCR, 0xC7);   // FIFO on, cleared, 14-byte threshold
    outb(COM1 + COM_MCR, 0x03);   // DTR + RTS
    // A floating bus (no UART fitted) reads back 0xFF; don't spin
    // forever waiting for THR-empty on such a machine.
    return insb(COM1 + COM_LSR) != 0xFF;
}

void arch_console_putc(char c){
    // Bounded wait: a stuck UART must not hang the kernel.
    for(int spins = 0; spins < 100000; spins++){
        if(insb(COM1 + COM_LSR) & COM_LSR_THR_EMPTY){
            break;
        }
    }
    outb(COM1 + COM_DATA, (unsigned char)c);
}

// ------------------------------------------------------------------
// Interrupt controller
// ------------------------------------------------------------------

// The LAPIC and IOAPIC replace the 8259s (kernel.asm
// remapped and masked them). Without a usable MADT, or with
// samos.legacy_pic, the 8259s stay.
void arch_irq_init(void){
    if(!boot_cmdline_has("samos.legacy_pic")){
        apic_init();
    }
}

bool arch_irq_routable(void){
    return apic_active();
}

void arch_irq_unmask(int irq){
    IRQ_enable(irq);
}

void arch_irq_mask(int irq){
    IRQ_disable(irq);
}

// Slot n is vector APIC_VECTOR_DISK_BASE + n, whose EOI
// interrupt_handler sends after the handler.
int arch_irq_attach_gsi(uint32_t gsi, uint16_t flags, unsigned slot, bool masked,
                        INTERRUPT_CALLBACK_FUNCTION handler){
    uint8_t vector = APIC_VECTOR_DISK_BASE + slot;
    if(!apic_active() || vector >= APIC_VECTOR_DISK_END){
        return -EINVARG;
    }
    idt_register_interrupt_callback(vector, handler);
    if(ioapic_route_gsi(gsi, vector, flags, masked) < 0){
        return -EIO;
    }
    return vector;
}

int arch_irq_set_cpu(uint32_t gsi, struct cpu* c){
    return ioapic_set_dest(gsi, c->hw_id);
}

// MSI: fixed delivery, edge, physical destination; the same
// vector and EOI as a GSI on that slot. xAPIC ids only (8 bits).
int arch_irq_msi(unsigned slot, struct cpu* c, INTERRUPT_CALLBACK_FUNCTION handler,
                 uint64_t* addr, uint32_t* data){
    uint8_t vector = APIC_VECTOR_DISK_BASE + slot;
    if(!apic_active() || vector >= APIC_VECTOR_DISK_END || c->hw_id > 0xFF){
        return -EINVARG;
    }
    if(handler){
        idt_register_interrupt_callback(vector, handler);
    }
    *addr = 0xFEE00000u | (c->hw_id << 12);
    *data = vector;
    return vector;
}

// The LAPIC TPR is APIC_TPR_DISK_WAIT meanwhile, so only the disk
// vectors (0x50-0x5F) are taken. The state is the old TPR and RFLAGS.IF.
uint64_t arch_irq_wait_begin(void){
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0" : "=r"(rflags));
    uint32_t tpr = lapic_set_tpr(APIC_TPR_DISK_WAIT);
    return ((uint64_t)tpr << 32) | (rflags & 0x200);
}

// The nop gives the one-instruction sti shadow something to cover.
void arch_irq_wait_window(void){
    __asm__ volatile("sti; nop; cli; pause" ::: "memory");
}

void arch_irq_wait_end(uint64_t state){
    lapic_set_tpr((uint32_t)(state >> 32));
    if(state & 0x200){
        __asm__ volatile("sti");
    }
}

// ------------------------------------------------------------------
// Timer and time
// ------------------------------------------------------------------

// PIT channel 0 on IRQ0, unmasked.
// Interrupts stay off in the kernel until the first iretq into a task
// (or the idle loop) sets IF.
// With the APIC, the LAPIC timer (calibrated against PIT
// channel 2) instead, at the same rate. If the LAPIC timer measures 0,
// the PIT ticks through the IOAPIC (IRQ_enable routes IRQ0 to its
// GSI); samos.lapic_timer_fail forces that path for e2e/179.
bool arch_timer_start(uint32_t hz){
    smp_tick_hz = hz;
    if(apic_active() && !boot_cmdline_has("samos.lapic_timer_fail") &&
       lapic_timer_calibrate() == 0){
        lapic_timer_start(hz);
        return true;
    }
    serial_write(apic_active() ? "apic: IOAPIC with PIT tick\n"
                               : "apic: legacy 8259 and PIT tick\n");
    pit_init(hz);
    IRQ_enable(IRQ_TIMER);
    return false;
}

uint64_t arch_counter(void){
    return read_tsc();
}

uint64_t arch_counter_hz(void){
    return tsc_frequency();
}

TIME_MICROSECONDS arch_time_us(void){
    return tsc_microseconds();
}

TIME_MICROSECONDS arch_uptime_us(void){
    return tsc_uptime_microseconds();
}

// ------------------------------------------------------------------
// Firmware tables, port I/O and PCI configuration space
// ------------------------------------------------------------------

// The ACPI tables from the loader's RSDP.
void arch_firmware_init(void){
    acpi_init(samos_boot_info_ptr->acpi_rsdp);
}

uint8_t  arch_pio_read8(uint16_t port)  { return insb(port); }
uint16_t arch_pio_read16(uint16_t port) { return insw(port); }
uint32_t arch_pio_read32(uint16_t port) { return insdw(port); }
void arch_pio_write8(uint16_t port, uint8_t v)   { outb(port, v); }
void arch_pio_write16(uint16_t port, uint16_t v) { outw(port, v); }
void arch_pio_write32(uint16_t port, uint32_t v) { outdw(port, v); }

// Lecture 178 - configuration mechanism #1: enable bit, bus, device,
// function and dword offset to 0xCF8, then the dword at 0xCFC.
#define PCI_CFG_ADDRESS  0xCF8
#define PCI_CFG_DATA     0xCFC

static uint32_t pci_cfg_address(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off){
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
           ((uint32_t)fn << 8) | (off & 0xFCu);
}

uint32_t arch_pci_cfg_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off){
    outdw(PCI_CFG_ADDRESS, pci_cfg_address(bus, dev, fn, off));
    return insdw(PCI_CFG_DATA);
}

void arch_pci_cfg_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off, uint32_t v){
    outdw(PCI_CFG_ADDRESS, pci_cfg_address(bus, dev, fn, off));
    outdw(PCI_CFG_DATA, v);
}

// ------------------------------------------------------------------
// Power-off and reset after the ACPI ways
// ------------------------------------------------------------------

// Port 0x80 is the POST port: a write takes about 1 us on real
// hardware, without the timer or a calibrated TSC.
static void io_wait(uint32_t n){
    while(n--){
        outb(0x80, 0);
    }
}

// QEMU's fixed PM1 ports: 0x604 (ICH9 / q35 and newer pc firmware)
// and 0xB004 (Bochs, old QEMU).
void arch_shutdown_fallback(void){
    serial_write("acpi: shutdown via QEMU PM ports\n");
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    io_wait(100000);
}

// The 0xCF9 reset control register (PIIX/ICH), then the 8042 pulse,
// then a triple fault.
void arch_reboot_fallback(void){
    serial_write("acpi: reboot via port 0xcf9\n");
    outb(0xCF9, 0x02);
    io_wait(10);
    outb(0xCF9, 0x06);
    io_wait(100000);

    serial_write("acpi: reboot via 8042\n");
    for(int i = 0; i < 100000 && (insb(0x64) & 0x02); i++){
        io_wait(1);
    }
    outb(0x64, 0xFE);
    io_wait(100000);

    serial_write("acpi: reboot via triple fault\n");
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) null_idt = { 0, 0 };
    __asm__ volatile("lidt %0; int3" : : "m"(null_idt));
}
