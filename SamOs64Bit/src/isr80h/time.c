// Lecture 198 - SYSTEM_COMMAND25_UDELAY. Pop a microsecond
// count off the task stack, mark the task asleep, hand control
// to the next non-sleeping task.

#include "time.h"
#include "task/task.h"
#include "task/process.h"
#include "io/tsc.h"

void* isr80h_command25_udelay(struct trap_frame* frame){
    uint64_t microseconds = (uint64_t)task_get_stack_item(task_current(), 0);
    task_sleep(task_current(), microseconds);
    // The task resumes through task_enter, not the syscall
    // return path, so its return value goes into the saved frame.
    task_current()->registers.rax = 0;
    task_next();
    // Never reached - task_next jumps into the next task.
    return NULL;
}
