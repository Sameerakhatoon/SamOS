#ifndef ISR80H_WINDOW_H
#define ISR80H_WINDOW_H
// Lecture 154 - userland window-create syscall.

struct interrupt_frame;
void* isr80h_command16_window_create(struct interrupt_frame* frame);
// Lecture 158 - divert stdout to a window.
void* isr80h_command17_sysout_to_window(struct interrupt_frame* frame);
// Lecture 163 - drain a window event into a userland buffer.
void* isr80h_command18_get_window_event(struct interrupt_frame* frame);
// Lecture 165 - return a userland_graphics projection of a window's body.
void* isr80h_command19_window_graphics_get(struct interrupt_frame* frame);

#endif
