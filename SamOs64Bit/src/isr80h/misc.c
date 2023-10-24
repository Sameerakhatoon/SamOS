#include "misc.h"
#include "idt/idt.h"
#include "task/task.h"
#include "bootmarker.h"

// SYSTEM_COMMAND0_SUM: return the sum of the two arguments.
// Arguments come in rdi and rsi (isr80h_arg), not off
// the user stack, and the sum is 64-bit.
void* isr80h_command0_sum(struct trap_frame* frame){
    uint64_t v1 = (uint64_t)isr80h_arg(frame, 0);
    uint64_t v2 = (uint64_t)isr80h_arg(frame, 1);
    return (void*)(v1 + v2);
}

// SamOs e2e (Phase 2): userspace marker write,
// samos_e2e_mark(slot, value).
//
// Slot in rdi, value in rsi. The stack version had them the
// other way round (item 0 = the last push = value).
//
// The kernel restricts the slot range to BM_USER_FEATURE_BASE
// (40) up through BM_STAGE_MAX-1 so a buggy userspace program
// cannot overwrite the kernel-side stages or feature slots.
// Returns 0 on success, -1 on out-of-range.
void* isr80h_command26_e2e_mark(struct trap_frame* frame){
    uint32_t slot  = (uint32_t)(intptr_t)isr80h_arg(frame, 0);
    uint32_t value = (uint32_t)(intptr_t)isr80h_arg(frame, 1);
    if (slot < BM_USER_FEATURE_BASE || slot >= BM_STAGE_MAX) {
        return (void*)(intptr_t)-1;
    }
    boot_marker_set((boot_marker_stage_t)slot, value);
    return (void*)0;
}
