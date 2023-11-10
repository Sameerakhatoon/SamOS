#ifndef KERNEL_ISR80H_GRAPHICS_H
#define KERNEL_ISR80H_GRAPHICS_H
// Lecture 165 - userspace projection of a kernel graphics_info.
// Userland gets back a userland_graphics struct it owns; the
// real kernel graphics pointer hides behind a userland_ptr
// sentinel (L153) so userland never sees raw kernel addresses.

#include <stdint.h>
#include <stddef.h>

struct process;
struct graphics_info;

struct userland_graphics {
    size_t x;
    size_t y;
    size_t width;
    size_t height;

    // L166 will hand the pixel buffer back through this slot.
    void*  pixels;

    // Sentinel handle for the kernel graphics_info.
    void*  userland_ptr;
};

struct userland_graphics* isr80h_graphics_make_userland_metadata(struct process* process,
                                                                 struct graphics_info* graphics_info);

#endif
