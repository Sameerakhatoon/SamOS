#ifndef GRAPHICS_TERMINAL_H
#define GRAPHICS_TERMINAL_H
// Lecture 96 - text terminal surface. Owns a backing surface, a
// cursor (row, col), and a snapshot of the pixels it covered so
// the area can be restored on close. The L96 introduction lands
// the type and a half-finished constructor; the API gets
// completed in L97-L100.

#include <stdint.h>
#include <stddef.h>
#include "graphics/font.h"
#include "graphics/graphics.h"

enum {
    TERMINAL_FLAG_BACKSPACE_ALLOWED = 0b00000001
};

struct terminal {
    struct graphics_info*     graphics_info;

    // Snapshot of the surface's pixels at create time, used to
    // restore the area when the terminal is destroyed. NULL
    // until the snapshot is taken.
    struct framebuffer_pixel* terminal_background;

    struct {
        size_t row;
        size_t col;
    } text;

    struct {
        size_t abs_x;
        size_t abs_y;
        size_t width;
        size_t height;
    } bounds;

    struct font*              font;
    struct framebuffer_pixel  font_color;
    int                       flags;
};

#endif
