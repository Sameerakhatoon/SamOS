#include "misc.h"
#include "idt/idt.h"
#include "task/task.h"

// SYSTEM_COMMAND0_SUM: pull two integers off the user task's stack
// (last-push at index 0, push-before-that at index 1) and return
// their sum in EAX.
void* isr80h_command0_sum(struct interrupt_frame* frame){
    // L54 - widen the cast: stack items are void* (8 bytes on
    // x86_64). Going through intptr_t avoids the truncation
    // diagnostic; the user-side int still fits in the low 32
    // bits.
    int v2 = (int)(intptr_t)task_get_stack_item(task_current(), 1);
    int v1 = (int)(intptr_t)task_get_stack_item(task_current(), 0);
    return (void*)(intptr_t)(v1 + v2);
}
