#ifndef PERCPU_H
#define PERCPU_H
// Per-CPU data. Not in the book.
//
// Each CPU keeps a pointer to its struct cpu where arch_cpu_self()
// finds it in one load (x86: the GS base, aarch64: TPIDR_EL1,
// riscv64: tp); the struct's first field points at itself. On x86 the
// trap stubs swapgs on entry from ring 3 and before the iretq back, so
// ring 3 never sees the kernel's GS base and the kernel always does
// (kernel_registers and user_registers no longer load gs, which
// would zero the base).
//
// cpus[0] is the BSP; the others are numbered in firmware order (x86:
// MADT) as they start.

#include <stdint.h>
#include <stdbool.h>
#include "arch/arch.h"

#define SAMOS_MAX_CPUS        16
#define CPU_MAX_HELD_LOCKS    16
#define CPU_SERIAL_LINE       200

struct task;
struct paging_desc;
struct spinlock;

struct cpu {
    struct cpu*          self;            // arch_cpu_self() reads this
    uint32_t             index;           // 0 = BSP
    uint32_t             hw_id;           // x86: APIC id (aarch64: MPIDR, riscv64: hart id)
    volatile uint32_t    online;

    // Scheduler state, under the sched lock (task.c).
    struct task*         current;         // NULL while idle
    struct task*         run_head;        // READY tasks for this CPU
    struct task*         run_tail;
    uint32_t             run_count;
    struct task*         sleep_head;      // SLEEPING tasks this CPU wakes

    // The address space this CPU has loaded, and the process
    // descriptor it is running in user mode (NULL in the kernel): who
    // needs a shootdown when a user mapping changes (paging.c).
    struct paging_desc*  paging_desc;
    struct paging_desc* volatile user_desc;

    // Locks held, for the order check (spinlock.c).
    struct spinlock*     held[CPU_MAX_HELD_LOCKS];
    int                  held_count;

    // Cross-CPU requests (smp.c).
    volatile uint32_t    tlb_pending;
    void (* volatile     call_fn)(void*);
    void*                call_arg;
    volatile uint32_t    call_done;

    // Statistics, read by syscall 51 (cpu_stats).
    volatile uint64_t    ticks_busy;      // timer ticks with a task current
    volatile uint64_t    ticks_idle;
    volatile uint64_t    tasks_entered;
    volatile uint64_t    steals;          // tasks taken from another CPU's queue
    volatile uint64_t    ipi_resched;
    volatile uint64_t    ipi_tlb;

    // Console output is buffered per CPU up to a newline once the
    // other CPUs run, so lines from two CPUs never mix (serial.c).
    char                 line[CPU_SERIAL_LINE];
    uint32_t             line_len;

    // The arch's own per-CPU state (x86: kernel stack, GDT
    // copy, TSS).
    struct arch_cpu      arch;
} __attribute__((aligned(64)));

extern struct cpu cpus[SAMOS_MAX_CPUS];
extern volatile uint32_t cpu_online_count;

static inline struct cpu* this_cpu(void){
    return (struct cpu*)arch_cpu_self();
}

// More than one CPU is running.
static inline bool smp_active(void){
    return cpu_online_count > 1;
}

#endif
