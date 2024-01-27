#include "misc.h"
#include "isr80h/isr80h.h"
#include "task/task.h"
#include "bootmarker.h"
#include "task/process.h"
#include "task/uaccess.h"
#include "cpu/percpu.h"
#include "memory/heap/kheap.h"
#include "status.h"

// SYSTEM_COMMAND0_SUM: return the sum of the two arguments.
// Arguments come in rdi and rsi (isr80h_arg), not off
// the user stack, and the sum is 64-bit.
void* isr80h_command0_sum(struct trap_frame* frame){
    uint64_t v1 = (uint64_t)isr80h_arg(frame, 0);
    uint64_t v2 = (uint64_t)isr80h_arg(frame, 1);
    return (void*)(v1 + v2);
}

// SamOs e2e (Phase 2): userspace marker write,
// samos_e2e_mark(slot, value).
//
// Slot in rdi, value in rsi. The stack version had them the
// other way round (item 0 = the last push = value).
//
// The kernel restricts the slot range to BM_USER_FEATURE_BASE
// (40) up through BM_STAGE_MAX-1 so a buggy userspace program
// cannot overwrite the kernel-side stages or feature slots.
// Returns 0 on success, -1 on out-of-range.
void* isr80h_command26_e2e_mark(struct trap_frame* frame){
    uint32_t slot  = (uint32_t)(intptr_t)isr80h_arg(frame, 0);
    uint32_t value = (uint32_t)(intptr_t)isr80h_arg(frame, 1);
    if (slot < BM_USER_FEATURE_BASE || slot >= BM_STAGE_MAX) {
        return (void*)(intptr_t)-1;
    }
    boot_marker_set((boot_marker_stage_t)slot, value);
    return (void*)0;
}

// Per-CPU counters, for the CPU-bound test (e2e/191):
// eight 64-bit values per CPU, in this order. Returns the number of
// CPUs online; at most `max` entries are written.
struct cpu_stat {
    uint64_t index, apic_id, ticks_busy, ticks_idle;
    uint64_t tasks_entered, steals, ipi_resched, ipi_tlb;
};

void* isr80h_command51_cpu_stats(struct trap_frame* frame){
    struct process* process = task_current()->process;
    struct cpu_stat* out = isr80h_arg(frame, 0);
    long max = (long)isr80h_arg(frame, 1);
    long n = 0;
    for(int i = 0; i < SAMOS_MAX_CPUS && n < max; i++){
        struct cpu* c = &cpus[i];
        if(!c->online){
            continue;
        }
        struct cpu_stat s = {
            c->index, c->hw_id, c->ticks_busy, c->ticks_idle,
            c->tasks_entered, c->steals, c->ipi_resched, c->ipi_tlb,
        };
        if(copy_to_user(process, &out[n], &s, sizeof(s)) < 0){
            return ERROR(-EFAULT);
        }
        n++;
    }
    return (void*)(intptr_t)cpu_online_count;
}

// Frames and slab objects in use, for the stress test's leak check
// (e2e/192). The layout is struct kheap_stats.
void* isr80h_command52_mem_stats(struct trap_frame* frame){
    struct kheap_stats s;
    kheap_get_stats(&s);
    return ERROR(copy_to_user(task_current()->process, isr80h_arg(frame, 0), &s, sizeof(s)));
}

void* isr80h_command53_cpu_current(struct trap_frame* frame){
    return (void*)(intptr_t)this_cpu()->index;
}
