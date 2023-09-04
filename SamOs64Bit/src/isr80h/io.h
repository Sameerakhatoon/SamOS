#ifndef ISR80H_IO_H
#define ISR80H_IO_H

struct trap_frame;

void* isr80h_command1_print(struct trap_frame* frame);
void* isr80h_command2_getkey(struct trap_frame* frame);
void* isr80h_command3_putchar(struct trap_frame* frame);

#endif
