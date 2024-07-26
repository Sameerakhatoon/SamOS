#ifndef SMP_H
#define SMP_H
// IPIs and TLB shootdown, cross-CPU calls, panic stop.
// Not in the book. The CPUs are started by
// arch_start_cpus (arch/arch.h).

#include <stdint.h>
#include <stdbool.h>
#include "cpu/percpu.h"

struct paging_desc;

void smp_send_resched(struct cpu* target);
// Run fn(arg) on `target` from its IPI handler and wait for it.
void smp_call(struct cpu* target, void (*fn)(void*), void* arg);
// Without the call lock or the wait: one caller per target at a time
// (the self-test starts every AP at once this way).
void smp_call_async(struct cpu* target, void (*fn)(void*), void* arg);
// Panics after SAMOS_LOCK_TIMEOUT_US (30 s) without an answer.
void smp_call_wait(struct cpu* target);
// Test only (samos.smp_call_ignore): CPU `index` drops its calls.
void smp_call_ignore(int index);
// Flush `va` on every CPU that may cache it: all CPUs for a
// kernel-half address, else the CPUs running `desc` in user mode.
void smp_tlb_shootdown(struct paging_desc* desc, uint64_t va);

// The arch's IPI handler for `ipi` (enum arch_ipi).
void smp_handle_ipi(enum arch_ipi ipi, struct trap_frame* frame);
// What ARCH_IPI_INPUT runs: input work an interrupt handler hands to
// a CPU because it may itself run inside a disk wait (virtio-input).
// Up to four, each run on every ARCH_IPI_INPUT (USB too). Six
// (the network's NIC work).
void smp_input_register(void (*fn)(void));

// Answer pending cross-CPU requests; spin loops call this.
static inline void smp_poll(struct cpu* c){
    extern void smp_tlb_service(struct cpu* c);
    if(c->tlb_pending){
        smp_tlb_service(c);
    }
}

// Panic: stop every other CPU (once), so a panic on one CPU halts the
// machine. The arch's stop handler (x86: the NMI) calls smp_nmi_stop.
void smp_panic_begin(void);
bool smp_nmi_stop(void);
bool smp_panicking(void);

#endif
