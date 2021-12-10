#ifndef GRAPHICS_TERMINAL_H
#define GRAPHICS_TERMINAL_H
// Lecture 96-97 - text terminal surface. Owns a backing surface,
// a glyph cursor (row, col), and a snapshot of the pixels it
// covered so the area can be restored on close. L97 grew the
// public API to the shape the L100 write path will need.

#include <stdint.h>
#include <stddef.h>
#include "graphics/font.h"
#include "graphics/graphics.h"

enum {
    TERMINAL_FLAG_BACKSPACE_ALLOWED = 0b00000001
};

struct terminal {
    struct graphics_info*     graphics_info;
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

void              terminal_system_setup(void);
struct terminal*  terminal_create(struct graphics_info* graphics_info,
                                  int starting_x, int starting_y,
                                  size_t width, size_t height,
                                  struct font* font,
                                  struct framebuffer_pixel font_color,
                                  int flags);
void              terminal_free(struct terminal* terminal);

struct terminal*  terminal_get_at_screen_position(size_t x, size_t y,
                                                  struct terminal* ignore_terminal);

void              terminal_background_save(struct terminal* terminal);
void              terminal_restore_background(struct terminal* terminal,
                                              int sx, int sy,
                                              int width, int height);

int               terminal_cursor_set(struct terminal* terminal, int row, int col);
int               terminal_cursor_row(struct terminal* terminal);
int               terminal_cursor_col(struct terminal* terminal);
int               terminal_total_cols(struct terminal* terminal);
int               terminal_total_rows(struct terminal* terminal);

#endif
