#ifndef KERNEL_ISR80H_TIME_H
#define KERNEL_ISR80H_TIME_H
// Lecture 198 - userland udelay syscall.

struct interrupt_frame;
void* isr80h_command25_udelay(struct interrupt_frame* frame);

#endif
