#include "misc.h"
#include "idt/idt.h"
#include "task/task.h"
#include "bootmarker.h"

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

// SamOs e2e (Phase 2): userspace marker write.
//
// Stack layout (last-push at index 0):
//   item 0: uint32_t value
//   item 1: uint32_t slot
//
// The kernel restricts the slot range to BM_USER_FEATURE_BASE
// (40) up through BM_STAGE_MAX-1 so a buggy userspace program
// cannot overwrite the kernel-side stages or feature slots.
// Returns 0 on success, -1 on out-of-range.
void* isr80h_command26_e2e_mark(struct interrupt_frame* frame){
    (void)frame;
    uint32_t value = (uint32_t)(intptr_t)task_get_stack_item(task_current(), 0);
    uint32_t slot  = (uint32_t)(intptr_t)task_get_stack_item(task_current(), 1);
    if (slot < BM_USER_FEATURE_BASE || slot >= BM_STAGE_MAX) {
        return (void*)(intptr_t)-1;
    }
    boot_marker_set((boot_marker_stage_t)slot, value);
    return (void*)0;
}
