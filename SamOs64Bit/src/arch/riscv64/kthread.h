#ifndef SAMOS_RISCV64_KTHREAD_H
#define SAMOS_RISCV64_KTHREAD_H

// Kernel threads switched by the timer, the proof that the
// trap path preempts. Not the kernel's scheduler (that is task.c,
// ported later); just enough to run busy loops side by side.
//
// Each thread has its own stack. A thread that is not running is the
// trap_frame its last trap left at the top of its stack; switching is
// returning another thread's frame from riscv64_trap. The boot context
// is the idle thread: it runs only when no other thread is ready.

#include <stdint.h>
#include "arch/riscv64/trap.h"

// Ready to run at the next tick. Returning from `fn` ends the thread.
// The thread's id (1..), or -1 when every slot is taken.
int  kthread_create(const char* name, void (*fn)(void* arg), void* arg);
// Times thread `id` was switched to (its time slices so far).
uint64_t kthread_slices(int id);
// Threads created and not yet ended.
int  kthread_alive(void);
// Timer tick: ask for a switch at the end of this trap.
void sched_tick(void);
// End of every interrupt: the frame to resume (the current thread's
// own `frame` unless a switch was asked for).
struct trap_frame* sched_switch(struct trap_frame* frame);
uint64_t sched_switches(void);
const char* kthread_name(void);

#endif
