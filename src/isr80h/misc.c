#include "misc.h"
#include "idt/idt.h"

// Placeholder syscall command. The real "sum two ints off the user
// stack" implementation lands once we have user-space argument
// extraction helpers.
void* isr80h_command0_sum(struct interrupt_frame* frame){
    return 0;
}
