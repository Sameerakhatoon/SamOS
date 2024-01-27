#ifndef KERNEL_APIC_H
#define KERNEL_APIC_H
// Local APIC and IOAPIC in place of the 8259 pair, and the
// LAPIC timer in place of the PIT tick.
//
// Not in the book, which runs on the 8259s. The MADT (acpi.h) gives
// the LAPIC and IOAPIC addresses and the ISA interrupt overrides.
// Vectors stay where the PIC put them: ISA IRQ n is still vector
// 0x20 + n, so every idt_register_interrupt_callback(0x20 + n) and
// IRQ_enable(n) works unchanged; the IOAPIC entry for IRQ n carries
// the GSI, polarity and trigger mode the overrides give.
//
// Everything named lapic_* acts on the LAPIC of the CPU that runs
// it (all LAPICs sit at the same physical address), so the SMP work calls
// lapic_init_cpu and lapic_timer_start on each AP as it starts.

#include <stdint.h>
#include <stdbool.h>

#define APIC_VECTOR_PIC_BASE   0x30   // 8259s remapped here and masked: spurious only
#define APIC_VECTOR_PIC_END    0x40
#define APIC_VECTOR_TIMER      0x40   // LAPIC timer, the scheduler tick
#define APIC_VECTOR_SPURIOUS   0xFF   // LAPIC spurious (low nibble must be 0xF on old CPUs)

// BSP: LAPIC on, every IOAPIC entry masked then the 16 ISA IRQs
// routed (masked) to this CPU, 8259s remapped and masked. Returns
// -ENOENT without a MADT, LAPIC or IOAPIC; the kernel then stays on
// the 8259s and the PIT.
int      apic_init(void);
bool     apic_active(void);

void     lapic_init_cpu(void);
uint32_t lapic_id(void);
void     lapic_eoi(void);

// Measure the LAPIC timer against PIT channel 2 (once, on the BSP), then
// run it periodic at hz on the calling CPU.
int      lapic_timer_calibrate(void);
void     lapic_timer_start(uint32_t hz);
uint64_t lapic_timer_hz(void);

// Unmask or mask ISA IRQ n at its IOAPIC entry (IRQ_enable's APIC path).
int      ioapic_set_masked(uint8_t isa_irq, bool masked);

// Disk completion interrupts. Each controller or ATA
// channel gets a vector in 0x50-0x5F (priority class 5); while a
// driver waits, the TPR is APIC_TPR_DISK_WAIT so only those are taken.
// Their EOI goes out after the handler has acked the device.
#define APIC_VECTOR_DISK_BASE  0x50
#define APIC_VECTOR_DISK_END   0x60
#define APIC_TPR_DISK_WAIT     0x40

// Route a GSI to `vector` on the BSP with MPS INTI flags (polarity,
// trigger). Returns -ENOENT without the APIC or an IOAPIC for the GSI.
int      ioapic_route_gsi(uint32_t gsi, uint8_t vector, uint16_t inti_flags, bool masked);
// Set this CPU's task priority; returns the old value.
uint32_t lapic_set_tpr(uint32_t tpr);

// IPIs: a fixed vector to one CPU, INIT and STARTUP for
// AP bring-up (page = trampoline physical address >> 12), and an NMI
// to every CPU but this one (panic).
void     lapic_send_ipi(uint32_t apic_id, uint8_t vector);
void     lapic_send_init(uint32_t apic_id);
void     lapic_send_startup(uint32_t apic_id, uint32_t page);
void     lapic_send_nmi_all_others(void);
// Point a routed GSI at another CPU (a disk wait steers
// its line to the CPU that issued the command).
int      ioapic_set_dest(uint32_t gsi, uint32_t apic_id);

#endif
