#ifndef KERNEL_ISR80H_FILE_H
#define KERNEL_ISR80H_FILE_H
// Lecture 105 - userland file syscalls. L105 lands fopen
// (command 10); L106 will add fclose, L107 fread, etc.

struct trap_frame;
void* isr80h_command10_fopen(struct trap_frame* frame);
void* isr80h_command11_fclose(struct trap_frame* frame);
void* isr80h_command12_fread(struct trap_frame* frame);
void* isr80h_command13_fseek(struct trap_frame* frame);
void* isr80h_command14_fstat(struct trap_frame* frame);

#endif
