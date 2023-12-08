#ifndef TASK_H
#define TASK_H

#include "config.h"
#include "memory/paging/paging.h"
#include "io/tsc.h"           // L197 - TIME_MICROSECONDS / tsc_microseconds
#include <stdbool.h>

struct process;
struct interrupt_frame;

// Lecture 40 - 64-bit register file.
struct registers {
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rax;

    uint64_t ip;
    uint64_t cs;
    uint64_t flags;
    uint64_t rsp;
    uint64_t ss;
};

struct task {
    // Lecture 113 - paging_desc moved from task to process.
    // task->process->paging_desc is the canonical pointer; the
    // accessor task_paging_desc() forwards to it.
    struct registers     registers;
    struct process*      process;

    // Lecture 197 - sleep state. task_get_next_non_sleeping_task
    // skips tasks whose deadline is still in the future.
    struct {
        TIME_MICROSECONDS sleep_until_microseconds;
    } sleeping;

    struct task*         next;
    struct task*         prev;
};

struct task* task_new(struct process* process);
struct task* task_current();
struct task* task_get_next();
int          task_free(struct task* task);

int   task_switch(struct task* task);
int   task_page();
int   task_page_task(struct task* task);
void  task_run_first_ever_task();
void  task_current_save_state(struct interrupt_frame* frame);
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
void task_return(struct registers* regs);
void restore_general_purpose_registers(struct registers* regs);
void user_registers();

#endif
