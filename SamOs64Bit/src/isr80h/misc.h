#ifndef ISR80H_MISC_H
#define ISR80H_MISC_H

struct trap_frame;

void* isr80h_command0_sum(struct trap_frame* frame);

// SamOs e2e (Phase 2): user-side selftest marker write. See the
// enum entry SYSTEM_COMMAND26_E2E_MARK in isr80h.h for the wire
// contract.
void* isr80h_command26_e2e_mark(struct trap_frame* frame);

// CPU and heap counters, and the caller's CPU.
void* isr80h_command51_cpu_stats(struct trap_frame* frame);
void* isr80h_command52_mem_stats(struct trap_frame* frame);
void* isr80h_command53_cpu_current(struct trap_frame* frame);

#endif
