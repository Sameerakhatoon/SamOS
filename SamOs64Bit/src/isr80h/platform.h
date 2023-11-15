#ifndef KERNEL_ISR80H_PLATFORM_H
#define KERNEL_ISR80H_PLATFORM_H
// Wall-clock time, power off and reset.

struct trap_frame;
void* isr80h_command48_time(struct trap_frame* frame);
void* isr80h_command49_shutdown(struct trap_frame* frame);
void* isr80h_command50_reboot(struct trap_frame* frame);

#endif
