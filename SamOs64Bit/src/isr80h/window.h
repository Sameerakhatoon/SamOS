#ifndef ISR80H_WINDOW_H
#define ISR80H_WINDOW_H
// Lecture 154 - userland window-create syscall.

// Lecture 173 - update-window subcommands. Userland selects
// which window field to change by passing one of these constants
// alongside the window handle and a value pointer.
enum {
    ISR80H_WINDOW_UPDATE_TITLE = 0,
};

struct interrupt_frame;
void* isr80h_command16_window_create(struct interrupt_frame* frame);
// Lecture 158 - divert stdout to a window.
void* isr80h_command17_sysout_to_window(struct interrupt_frame* frame);
// Lecture 163 - drain a window event into a userland buffer.
void* isr80h_command18_get_window_event(struct interrupt_frame* frame);
// Lecture 165 - return a userland_graphics projection of a window's body.
void* isr80h_command19_window_graphics_get(struct interrupt_frame* frame);
// Lecture 167 - userspace asks the compositor to redraw a window.
void* isr80h_command21_window_redraw(struct interrupt_frame* frame);
// Lecture 172 - userspace redraws a clip rect within the body.
void* isr80h_command23_window_redraw_region(struct interrupt_frame* frame);
// Lecture 173 - update one of the window's fields (currently just title).
void* isr80h_command24_update_window(struct interrupt_frame* frame);

#endif
