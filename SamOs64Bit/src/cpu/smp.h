#ifndef SMP_H
#define SMP_H
// Application processors, IPIs and TLB shootdown.
// Not in the book.

#include <stdint.h>
#include <stdbool.h>
#include "cpu/percpu.h"

// IPI vectors, clear of the 8259 range (0x30-0x3F), the LAPIC timer
// (0x40) and the LAPIC spurious vector (0xFF).
#define SMP_VECTOR_TLB       0xF1
#define SMP_VECTOR_RESCHED   0xF2
#define SMP_VECTOR_CALL      0xF3

struct paging_desc;

// BSP: per-CPU data, GDT, TSS and stacks for cpus[0].
void smp_bsp_init(void);
// BSP: start every enabled MADT processor (INIT-SIPI-SIPI). Returns
// the number of CPUs online. Needs the APIC; with the legacy 8259
// path it does nothing and returns 1.
int  smp_start_aps(void);

// The TSS, GDT entry and stacks of `c` (BSP and APs).
void cpu_tss_init(struct cpu* c);

void smp_register_ipis(void);
void smp_send_resched(struct cpu* target);
// Run fn(arg) on `target` from its IPI handler and wait for it.
void smp_call(struct cpu* target, void (*fn)(void*), void* arg);
// Without the call lock or the wait: one caller per target at a time
// (the self-test starts every AP at once this way).
void smp_call_async(struct cpu* target, void (*fn)(void*), void* arg);
void smp_call_wait(struct cpu* target);
// Flush `va` on every CPU that may cache it: all CPUs for a
// kernel-half address, else the CPUs running `desc` in ring 3.
void smp_tlb_shootdown(struct paging_desc* desc, uint64_t va);

// Answer pending cross-CPU requests; spin loops call this.
static inline void smp_poll(struct cpu* c){
    extern void smp_tlb_service(struct cpu* c);
    if(c->tlb_pending){
        smp_tlb_service(c);
    }
}

// Panic: stop every other CPU with an NMI (once), so a panic on one
// CPU halts the machine. The NMI handler calls smp_nmi_stop.
void smp_panic_begin(void);
bool smp_nmi_stop(void);
bool smp_panicking(void);

// Swap GS if it does not hold this CPU's base (an NMI or fault that
// hit between a swapgs and its iretq).
void smp_fix_gs(void);

#endif
