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

#endif
