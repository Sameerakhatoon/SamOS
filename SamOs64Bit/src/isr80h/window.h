#ifndef ISR80H_WINDOW_H
#define ISR80H_WINDOW_H
// Lecture 154 - userland window-create syscall.

struct interrupt_frame;
void* isr80h_command16_window_create(struct interrupt_frame* frame);
// Lecture 158 - divert stdout to a window.
void* isr80h_command17_sysout_to_window(struct interrupt_frame* frame);

#endif
