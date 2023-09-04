#ifndef ISR80H_HEAP_H
#define ISR80H_HEAP_H

struct trap_frame;

void* isr80h_command4_malloc(struct trap_frame* frame);
void* isr80h_command5_free(struct trap_frame* frame);
void* isr80h_command15_realloc(struct trap_frame* frame);

#endif
