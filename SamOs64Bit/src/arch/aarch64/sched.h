#ifndef ARCH_AARCH64_SCHED_H
#define ARCH_AARCH64_SCHED_H

#include <stdint.h>
#include "arch/aarch64/trap.h"

// Preemptive round robin over kernel threads, enough to prove
// the timer IRQ switches contexts before there is an MMU or
// user mode; the shared scheduler (task/) takes over at the hardware layer.
//
// Thread 0 is the boot thread, which becomes the idle thread: it runs
// only when no other thread is runnable, and sleeps in wfi. A thread's
// saved context is the trap_frame on its own stack.

#define SCHED_THREADS 4

void     sched_init(void);
// New thread at entry(arg) on [stack, stack + size); returning from
// entry ends it. Thread id, or -1 when the table is full.
int      sched_spawn(void (*entry)(void* arg), void* arg, void* stack, uint64_t size);
void     sched_tick(struct trap_frame* f);   // timer IRQ: switch at the end of this trap
// End of an IRQ: save `f` as the current thread's context and return
// the frame to resume.
struct trap_frame* sched_switch(struct trap_frame* f);
void     sched_exit(void) __attribute__((noreturn));
int      sched_runnable(void);  // threads other than idle still runnable
uint64_t sched_switches(void);
int      sched_current(void);

#endif
