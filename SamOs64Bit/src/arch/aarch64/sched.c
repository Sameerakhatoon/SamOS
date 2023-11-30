// Kernel thread round robin. See sched.h.
#include "arch/aarch64/sched.h"

enum { THREAD_FREE, THREAD_RUNNABLE, THREAD_DEAD };

struct thread {
    struct trap_frame* frame;   // saved context, on the thread's stack
    int state;
};

static struct thread threads[SCHED_THREADS];
static int current;
static int active;
static volatile int need_switch;
static volatile uint64_t switches;

void sched_init(void)
{
    threads[0].state = THREAD_RUNNABLE;     // the boot thread, now idle
    current = 0;
    active = 1;
}

int sched_spawn(void (*entry)(void* arg), void* arg, void* stack, uint64_t size)
{
    uint64_t daif = irq_save();
    int id = -1;
    for (int i = 1; i < SCHED_THREADS; i++) {
        if (threads[i].state == THREAD_FREE) {
            id = i;
            break;
        }
    }
    if (id > 0) {
        // The first switch to the thread "returns" from this frame:
        // eret to entry(arg) at EL1h with IRQs open, x30 = sched_exit.
        uint64_t top = ((uint64_t)stack + size) & ~15ull;
        struct trap_frame* f = (struct trap_frame*)(top - TRAP_FRAME_SIZE);
        for (int i = 0; i < 31; i++)
            f->x[i] = 0;
        f->x[0] = (uint64_t)arg;
        f->x[30] = (uint64_t)sched_exit;
        f->sp = top;
        f->elr = (uint64_t)entry;
        f->spsr = SPSR_DAF | SPSR_EL1H;
        f->esr = f->far = 0;
        threads[id].frame = f;
        threads[id].state = THREAD_RUNNABLE;
    }
    irq_restore(daif);
    return id;
}

void sched_tick(void)
{
    need_switch = 1;
}

struct trap_frame* sched_switch(struct trap_frame* f)
{
    if (!active || !need_switch)
        return f;
    need_switch = 0;
    threads[current].frame = f;
    // The next runnable thread after this one; idle only if none.
    int next = 0;
    for (int k = 1; k <= SCHED_THREADS; k++) {
        int i = (current + k) % SCHED_THREADS;
        if (i != 0 && threads[i].state == THREAD_RUNNABLE) {
            next = i;
            break;
        }
    }
    if (next != current)
        switches++;
    current = next;
    return threads[next].frame;
}

void sched_exit(void)
{
    irq_save();
    threads[current].state = THREAD_DEAD;
    irq_enable();
    for (;;)
        __asm__ volatile("wfi");    // until the next tick switches away for good
}

int sched_runnable(void)
{
    int n = 0;
    for (int i = 1; i < SCHED_THREADS; i++)
        n += threads[i].state == THREAD_RUNNABLE;
    return n;
}

uint64_t sched_switches(void)
{
    return switches;
}

int sched_current(void)
{
    return current;
}
