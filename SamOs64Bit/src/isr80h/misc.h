#ifndef ISR80H_MISC_H
#define ISR80H_MISC_H

struct interrupt_frame;

void* isr80h_command0_sum(struct interrupt_frame* frame);

// SamOs e2e (Phase 2): user-side selftest marker write. See the
// enum entry SYSTEM_COMMAND26_E2E_MARK in isr80h.h for the wire
// contract.
void* isr80h_command26_e2e_mark(struct interrupt_frame* frame);

#endif
