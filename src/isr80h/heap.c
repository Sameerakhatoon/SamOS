#include "heap.h"
#include "task/task.h"
#include "task/process.h"
#include <stddef.h>

void* isr80h_command4_malloc(struct interrupt_frame* frame){
    // L54 - widen the cast: stack items are void* (8 bytes on
    // x86_64), and the user is asking for a size_t. Going
    // through intptr_t avoids the pointer-to-int-of-different-
    // size diagnostic and preserves any size > 2 GiB.
    size_t size = (size_t)(uintptr_t)task_get_stack_item(task_current(), 0);
    return process_malloc(task_current()->process, size);
}

void* isr80h_command5_free(struct interrupt_frame* frame){
    void* ptr_to_free = task_get_stack_item(task_current(), 0);
    process_free(task_current()->process, ptr_to_free);
    return 0;
}
