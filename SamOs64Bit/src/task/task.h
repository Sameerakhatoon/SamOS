#ifndef TASK_H
#define TASK_H

#include "config.h"
#include "idt/idt.h"          // Struct trap_frame
#include "memory/paging/paging.h"
#include "io/tsc.h"           // L197 - TIME_MICROSECONDS / tsc_microseconds
#include <stdbool.h>

struct process;

// fxsave/fxrstor image: x87, MXCSR and xmm0-15.
#define TASK_FPU_STATE_SIZE 512

struct task {
    // The saved user state is a full trap_frame (all
    // 15 general registers), not the book's 7-register struct.
    // Lecture 113 - paging_desc moved from task to process.
    struct trap_frame    registers;
    struct process*      process;

    // Lecture 197 - sleep state. task_get_next_non_sleeping_task
    // skips tasks whose deadline is still in the future.
    struct {
        TIME_MICROSECONDS sleep_until_microseconds;
    } sleeping;

    struct task*         next;
    struct task*         prev;

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
int   task_page_task(struct task* task);
void  task_run_first_ever_task();
void  task_enter(struct task* task);
void  task_current_save_state(struct trap_frame* frame);
void* task_get_stack_item(struct task* task, int index);
int   copy_string_from_task(struct task* task, void* virtual, void* phys, int max);
void* task_virtual_address_to_physical(struct task* task, void* virtual_address);
void  task_next();

// Lecture 197 - sleep API.
void  task_sleep(struct task* task, TIME_MICROSECONDS microseconds);
bool  task_asleep(struct task* task);
int   task_get_next_non_sleeping_task(struct task** task_out);

// Lecture 40 - paging_desc accessors.
struct paging_desc* task_paging_desc(struct task* task);
struct paging_desc* task_current_paging_desc();

// Defined in task.asm.
void task_return(struct trap_frame* regs);
void user_registers();

#endif
