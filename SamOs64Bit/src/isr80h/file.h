#ifndef KERNEL_ISR80H_FILE_H
#define KERNEL_ISR80H_FILE_H
// Lecture 105 - userland file syscalls. L105 lands fopen
// (command 10); L106 will add fclose, L107 fread, etc.

struct interrupt_frame;
void* isr80h_command10_fopen(struct interrupt_frame* frame);
void* isr80h_command11_fclose(struct interrupt_frame* frame);

#endif
