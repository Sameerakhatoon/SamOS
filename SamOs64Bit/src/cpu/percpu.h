#ifndef PERCPU_H
#define PERCPU_H
// Per-CPU data. Not in the book.
//
// Each CPU's GS base points at its struct cpu, whose first field
// points at itself, so this_cpu() is one gs-relative load. The trap
// stubs swapgs on entry from ring 3 and before the iretq back, so
// ring 3 never sees the kernel's GS base and the kernel always does
// (kernel_registers and user_registers no longer load gs, which
// would zero the base).
//
// cpus[0] is the BSP; APs are numbered in MADT order as they start.

#include <stdint.h>
#include <stdbool.h>
#include "task/tss.h"

#define SAMOS_MAX_CPUS        16
#define CPU_MAX_HELD_LOCKS    16
#define CPU_SERIAL_LINE       200
#define CPU_KERNEL_STACK_SIZE (1024 * 1024)

struct task;
struct paging_desc;
struct spinlock;

struct cpu {
    struct cpu*          self;            // gs:0, read by this_cpu()
    uint32_t             index;           // 0 = BSP
    uint32_t             apic_id;
    volatile uint32_t    online;

    // Scheduler state, under the sched lock (task.c).
    struct task*         current;         // NULL while idle
    struct task*         run_head;        // READY tasks for this CPU
    struct task*         run_tail;
    uint32_t             run_count;
    struct task*         sleep_head;      // SLEEPING tasks this CPU wakes

    // The descriptor in CR3, and the process descriptor this CPU is
    // running in ring 3 (NULL in the kernel): who needs a shootdown
    // when a user mapping changes (paging.c).
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

    // COM1 output is buffered per CPU up to a newline once the APs
    // run, so lines from two CPUs never mix (serial.c).
    char                 line[CPU_SERIAL_LINE];
    uint32_t             line_len;

    uint8_t*             kernel_stack;    // TSS rsp0 stack, guard page at the bottom
    uint64_t             gdt[9];          // APs: own copy, TSS in slots 7 and 8
    struct tss           tss;
} __attribute__((aligned(64)));

extern struct cpu cpus[SAMOS_MAX_CPUS];
extern volatile uint32_t cpu_online_count;

static inline struct cpu* this_cpu(void){
    struct cpu* c;
    __asm__ volatile("mov %%gs:0, %0" : "=r"(c));
    return c;
}

// More than one CPU is running.
static inline bool smp_active(void){
    return cpu_online_count > 1;
}

#endif
