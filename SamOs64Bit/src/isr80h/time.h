#ifndef KERNEL_ISR80H_TIME_H
#define KERNEL_ISR80H_TIME_H
// Lecture 198 - userland udelay syscall.

struct trap_frame;
void* isr80h_command25_udelay(struct trap_frame* frame);
void* isr80h_command30_sleep_ms(struct trap_frame* frame);
void* isr80h_command31_uptime(struct trap_frame* frame);

#endif
