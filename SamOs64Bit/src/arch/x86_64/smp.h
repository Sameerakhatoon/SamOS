#ifndef ARCH_X86_64_SMP_H
#define ARCH_X86_64_SMP_H
// AP start-up, per-CPU TSS and GS, IPI vectors.
// The x86 half of cpu/smp.c; the portable half (shootdown
// bookkeeping, cross-CPU calls, panic stop) stays there.

#include "cpu/percpu.h"

// IPI vectors, clear of the 8259 range (0x30-0x3F), the LAPIC timer
// (0x40) and the LAPIC spurious vector (0xFF).
#define SMP_VECTOR_TLB       0xF1
#define SMP_VECTOR_RESCHED   0xF2
#define SMP_VECTOR_CALL      0xF3
// Priority class 4: a disk wait (APIC_TPR_DISK_WAIT) holds it off.
#define SMP_VECTOR_INPUT     0x48

// BSP: per-CPU data, GDT, TSS and stacks for cpus[0].
void smp_bsp_init(void);
// The TSS, GDT entry and stacks of `c` (BSP and APs).
void cpu_tss_init(struct cpu* c);
// The IPI vectors' handlers (idt_init).
void smp_register_ipis(void);
// Swap GS if it does not hold this CPU's base (an NMI or fault that
// hit between a swapgs and its iretq).
void smp_fix_gs(void);
// The tick rate arch_timer_start chose, for the APs' LAPIC timers.
extern uint32_t smp_tick_hz;

#endif
