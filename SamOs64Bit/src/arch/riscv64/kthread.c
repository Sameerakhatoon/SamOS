// Round-robin kernel threads on the timer tick.

#include <stdbool.h>
#include "arch/riscv64/kthread.h"
#include "arch/riscv64/riscv64.h"

#define KTHREAD_MAX   4
#define KTHREAD_STACK 8192

enum { KT_FREE, KT_READY, KT_DEAD };

struct kthread {
    int state;
    const char* name;
    struct trap_frame* frame;   // saved registers while not running
    uint64_t slices;            // times switched to
};

// [0] is the idle thread (the boot context); its frame is filled the
// first time a tick switches away from it.
static struct kthread threads[KTHREAD_MAX + 1] = { [0] = { KT_READY, "idle", 0 } };
static uint8_t stacks[KTHREAD_MAX][KTHREAD_STACK] __attribute__((aligned(16)));
static int current;
static int last_run = KTHREAD_MAX;  // round robin resumes after it
static bool need_switch;
static uint64_t switches;

// Where a thread function returns to (its initial ra). It stays on its
// own stack, with interrupts on, until the next tick switches away for
// good.
static void kthread_exit(void)
{
    uint64_t flags = irq_save();
    threads[current].state = KT_DEAD;
    irq_restore(flags);
    for (;;) {
        wfi();
    }
}

int kthread_create(const char* name, void (*fn)(void* arg), void* arg)
{
    for (int i = 1; i <= KTHREAD_MAX; i++) {
        if (threads[i].state != KT_FREE) {
            continue;
        }
        uint8_t* top = stacks[i - 1] + KTHREAD_STACK;
        struct trap_frame* f = (struct trap_frame*)top - 1;
        uint64_t* w = (uint64_t*)f;
        for (unsigned k = 0; k < sizeof(*f) / 8; k++) {
            w[k] = 0;
        }
        // As if a trap had interrupted the thread at its first
        // instruction: S-mode (SPP), interrupts on after sret (SPIE).
        f->sepc = (uint64_t)fn;
        f->sstatus = SSTATUS_SPP | SSTATUS_SPIE;
        f->x[TF_RA] = (uint64_t)kthread_exit;
        f->x[TF_SP] = (uint64_t)top;
        f->x[TF_A0] = (uint64_t)arg;
        __asm__("mv %0, gp" : "=r"(f->x[TF_GP]));
        uint64_t flags = irq_save();
        threads[i] = (struct kthread){ KT_READY, name, f, 0 };
        irq_restore(flags);
        return i;
    }
    return -1;
}

int kthread_alive(void)
{
    int n = 0;
    for (int i = 1; i <= KTHREAD_MAX; i++) {
        n += threads[i].state == KT_READY;
    }
    return n;
}

void sched_tick(void)
{
    need_switch = true;
}

// The next ready thread after the last one that ran, else idle.
static int pick_next(void)
{
    for (int k = 1; k <= KTHREAD_MAX; k++) {
        int i = (last_run + k - 1) % KTHREAD_MAX + 1;
        if (threads[i].state == KT_READY) {
            return i;
        }
    }
    return 0;
}

struct trap_frame* sched_switch(struct trap_frame* frame)
{
    if (!need_switch) {
        return frame;
    }
    need_switch = false;
    threads[current].frame = frame;
    int next = pick_next();
    if (next != 0) {
        last_run = next;
    }
    if (next != current) {
        switches++;
        threads[next].slices++;
        current = next;
    }
    return threads[current].frame;
}

uint64_t sched_switches(void)
{
    return switches;
}

const char* kthread_name(void)
{
    return threads[current].name;
}

uint64_t kthread_slices(int id)
{
    return id >= 0 && id <= KTHREAD_MAX ? threads[id].slices : 0;
}
