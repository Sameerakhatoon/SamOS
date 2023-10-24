#ifndef ISR80H_PROCESS_H
#define ISR80H_PROCESS_H

struct trap_frame;

void* isr80h_command6_process_load_start(struct trap_frame* frame);
void* isr80h_command7_invoke_system_command(struct trap_frame* frame);
void* isr80h_command8_get_program_arguments(struct trap_frame* frame);
void* isr80h_command9_exit(struct trap_frame* frame);
void* isr80h_command27_getpid(struct trap_frame* frame);
void* isr80h_command28_waitpid(struct trap_frame* frame);
void* isr80h_command29_kill(struct trap_frame* frame);

#endif
