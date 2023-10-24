// Lecture 198 - SYSTEM_COMMAND25_UDELAY. Pop a microsecond
// count off the task stack, mark the task asleep, hand control
// to the next non-sleeping task.
// The count comes in rdi, and microseconds are real
// microseconds now that the TSC frequency is right.

#include "time.h"
#include "task/task.h"
#include "task/process.h"
#include "io/tsc.h"

// Sleep the current task and run something else; rax = 0 when it
// wakes. 0 just yields.
static void* isr80h_sleep(TIME_MICROSECONDS microseconds){
    struct task* task = task_current();
    // The task resumes through task_enter, not the syscall
    // return path, so its return value goes into the saved frame.
    task->registers.rax = 0;
    if(microseconds == 0){
        task_ready(task);
    } else {
        task_sleep(task, microseconds);
    }
    task_next();
    // Never reached - task_next jumps into the next task.
    return NULL;
}

void* isr80h_command25_udelay(struct trap_frame* frame){
    return isr80h_sleep((uint64_t)isr80h_arg(frame, 0));
}

// Void sleep_ms(unsigned long ms). The 100 Hz tick wakes the
// task within 10 ms of the deadline.
void* isr80h_command30_sleep_ms(struct trap_frame* frame){
    uint64_t ms = (uint64_t)isr80h_arg(frame, 0);
    if(ms > (uint64_t)-1 / 1000){
        ms = (uint64_t)-1 / 1000;
    }
    return isr80h_sleep(ms * 1000);
}

// Unsigned long uptime(void): milliseconds since the TSC was
// calibrated at boot.
void* isr80h_command31_uptime(struct trap_frame* frame){
    return (void*)(tsc_uptime_microseconds() / 1000);
}
