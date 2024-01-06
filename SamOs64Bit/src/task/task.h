#ifndef TASK_H
#define TASK_H

#include "config.h"
#include "idt/idt.h"          // Struct trap_frame
#include "memory/paging/paging.h"
#include "io/tsc.h"           // L197 - TIME_MICROSECONDS / tsc_microseconds
#include <stdbool.h>

struct process;
struct cpu;

// fxsave/fxrstor image: x87, MXCSR and xmm0-15.
#define TASK_FPU_STATE_SIZE 512

// Scheduler states. A READY task is on a CPU's run
// queue, a RUNNING one is that CPU's current task, a SLEEPING one is
// on the sleep list of the CPU it slept on, a BLOCKED one is on no
// list until task_wake. A NEW task is loaded but not started
// (task_start), so no CPU can run it before its arguments are in.
enum task_state {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_SLEEPING,
    TASK_NEW,
};

// What a BLOCKED task waits for.
enum task_block_reason {
    TASK_BLOCK_NONE,
    TASK_BLOCK_KEY,     // getkey_block: a key for its process
    TASK_BLOCK_WAIT,    // waitpid: a child to exit
};

struct task {
    // The saved user state is a full trap_frame (all
    // 15 general registers), not the book's 7-register struct.
    // Lecture 113 - paging_desc moved from task to process.
    struct trap_frame    registers;
    struct process*      process;

    // Lecture 197 - sleep state. The deadline of a SLEEPING
    // task; the timer tick readies it once the deadline has passed.
    struct {
        TIME_MICROSECONDS sleep_until_microseconds;
    } sleeping;

    int                  state;          // enum task_state
    int                  block_reason;   // enum task_block_reason

    // The CPU whose queue or sleep list holds the task,
    // or that runs it; and a kill from another CPU, carried out by
    // the task's own CPU the next time it enters the kernel or is
    // picked to run. Under the sched lock.
    struct cpu*          cpu;
    int                  kill_pending;
    int                  kill_status;

    // Every task, in creation order.
    struct task*         next;
    struct task*         prev;

    // Run queue link (FIFO of READY tasks). Also the
    // sleep list link; a task is on at most one list.
    struct task*         run_next;

    // Saved on every trap from ring 3, restored before
    // the iretq into this task. The kernel itself is built with
    // -mgeneral-regs-only, so it never touches these registers.
    uint8_t              fpu_state[TASK_FPU_STATE_SIZE] __attribute__((aligned(16)));
};

struct task* task_new(struct process* process);
struct task* task_current();
struct task* task_get_next();
int          task_free(struct task* task);

int   task_switch(struct task* task);
int   task_page();
void  task_run_first_ever_task();
void  task_enter(struct task* task);
void  task_current_save_state(struct trap_frame* frame);
void  task_next();

// Scheduler.
void  task_ready(struct task* task);
void  task_wake(struct task* task);
void  task_block(struct task* task, int reason);
void  task_wake_sleepers(void);
bool  task_run_queue_empty(void);
void  task_tick(struct trap_frame* frame);

// Multi-CPU scheduling.
void  task_start(struct task* task);            // NEW -> a run queue
void  task_kill(struct task* task, int status); // ends it on its own CPU
void  task_resched(struct trap_frame* frame);   // reschedule IPI
void  task_check_killed(void);                  // after a trap from ring 3

// Lecture 197 - sleep API.
void  task_sleep(struct task* task, TIME_MICROSECONDS microseconds);
bool  task_asleep(struct task* task);

// Lecture 40 - paging_desc accessors.
struct paging_desc* task_paging_desc(struct task* task);
struct paging_desc* task_current_paging_desc();

// Defined in task.asm.
void task_return(struct trap_frame* regs);
void user_registers();

#endif
