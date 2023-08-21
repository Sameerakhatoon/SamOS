// Lecture 96 + 97 - text terminal.
//
// L97 completes the bookkeeping that L96 sketched out:
// terminal_free, terminal_get_at_screen_position,
// terminal_background_save, terminal_restore_background, cursor
// accessors / setter, and the size-in-glyph-cells helpers.
// L100 will hang the actual write path on top.

#include "graphics/terminal.h"
#include "memory/heap/kheap.h"
#include "graphics/font.h"
#include "graphics/graphics.h"
#include "graphics/image/image.h"
#include "lib/vector/vector.h"
#include "memory/memory.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static struct vector* terminal_vector = NULL;

// Forward decls so the static helpers above call into the
// public size accessors below.
int  terminal_total_cols(struct terminal* terminal);
int  terminal_total_rows(struct terminal* terminal);

inline static size_t terminal_abs_x_for_next_character(struct terminal* terminal){
    return terminal->bounds.abs_x
         + terminal->text.col * terminal->font->bits_width_per_character;
}

inline static size_t terminal_abs_y_for_next_character(struct terminal* terminal){
    return terminal->bounds.abs_y
         + terminal->text.row * terminal->font->bits_height_per_character;
}

void terminal_system_setup(void){
    terminal_vector = vector_new(sizeof(struct terminal*), 4, 0);
}

struct terminal* terminal_create(struct graphics_info* graphics_info,
                                 int starting_x, int starting_y,
                                 size_t width, size_t height,
                                 struct font* font,
                                 struct framebuffer_pixel font_color,
                                 int flags){
    int res = 0;
    if(font == NULL){
        return NULL;
    }
    if(graphics_info == NULL){
        return NULL;
    }
    if(starting_x < 0 || starting_y < 0
       || (uint32_t)starting_x >= graphics_info->horizontal_resolution
       || (uint32_t)starting_y >= graphics_info->vertical_resolution){
        return NULL;
    }

    struct terminal* terminal = kzalloc(sizeof(struct terminal));
    if(!terminal){
        res = -ENOMEM;
        goto out;
    }

    terminal->graphics_info       = graphics_info;
    terminal->terminal_background = NULL;
    terminal->text.row            = 0;
    terminal->text.col            = 0;
    terminal->bounds.abs_x        = starting_x;
    terminal->bounds.abs_y        = starting_y;
    terminal->bounds.width        = width;
    terminal->bounds.height       = height;
    terminal->font                = font;
    terminal->font_color          = font_color;
    terminal->flags               = flags;

    terminal_background_save(terminal);

    vector_push(terminal_vector, &terminal);

out:
    if(res < 0){
        terminal_free(terminal);
        terminal = NULL;
    }
    return terminal;
}

void terminal_free(struct terminal* terminal){
    if(!terminal){
        return;
    }
    if(terminal->terminal_background){
        kfree(terminal->terminal_background);
        terminal->terminal_background = NULL;
    }
    vector_pop_element(terminal_vector, &terminal, sizeof(terminal));
    kfree(terminal);
}

// Walk every registered terminal and return the first whose
// bounds enclose (x, y). `ignore_terminal` is skipped (used to
// avoid hit-testing yourself during a drag/focus walk).
//
// Upstream bug preserved: the bounds test uses
//   x <= terminal->bounds.width
// where it should be
//   x <= terminal->bounds.abs_x + terminal->bounds.width
// i.e. the right edge instead of the width. Same for y. We
// keep the upstream form so the diff lines up; the bug is
// inert today because nothing calls this function.
struct terminal* terminal_get_at_screen_position(size_t x, size_t y,
                                                 struct terminal* ignore_terminal){
    int res = 0;
    struct terminal* found_terminal = NULL;
    size_t total_children = vector_count(terminal_vector);
    for(size_t i = 0; i < total_children; i++){
        struct terminal* terminal = NULL;
        res = vector_at(terminal_vector, i, &terminal, sizeof(terminal));
        if(res < 0){
            goto out;
        }
        if(terminal == ignore_terminal){
            continue;
        }
        if(x >= terminal->bounds.abs_x && x <= terminal->bounds.width
           && y >= terminal->bounds.abs_y && y <= terminal->bounds.height){
            found_terminal = terminal;
            break;
        }
    }
out:
    if(res < 0){
        found_terminal = NULL;
    }
    return found_terminal;
}

// Copy the surface's current pixel buffer into our restore
// snapshot. Allocates the snapshot lazily on first call.
void terminal_background_save(struct terminal* terminal){
    size_t width        = terminal->bounds.width;
    size_t height       = terminal->bounds.height;
    size_t total_pixels = width * height;
    size_t buffer_size  = total_pixels * sizeof(struct framebuffer_pixel);

    if(!terminal->terminal_background){
        terminal->terminal_background = kzalloc(buffer_size);
        if(!terminal->terminal_background){
            return;
        }
    }
    memcpy(terminal->terminal_background, terminal->graphics_info->pixels, buffer_size);
}

static void terminal_handle_newline(struct terminal* terminal){
    terminal->text.row++;
    size_t total_rows_per_term = terminal_total_rows(terminal);
    if(terminal->text.row >= total_rows_per_term){
        terminal->text.row = 0;
    }
    terminal->text.col = 0;
}

static void terminal_update_position_after_draw(struct terminal* terminal){
    terminal->text.col += 1;
    size_t total_cols_per_row  = terminal_total_cols(terminal);
    size_t total_rows_per_term = terminal_total_rows(terminal);
    if(terminal->text.col >= total_cols_per_row){
        terminal->text.col = 0;
        terminal->text.row++;
    }
    if(terminal->text.row >= total_rows_per_term){
        terminal->text.col = 0;
        terminal->text.row = 0;
    }
}

int terminal_cursor_set(struct terminal* terminal, int row, int col){
    int res = 0;
    size_t total_cols_per_row  = terminal_total_cols(terminal);
    size_t total_rows_per_term = terminal_total_rows(terminal);
    if(col < 0 || (size_t)col >= total_cols_per_row){
        res = -EINVARG;
        goto out;
    }
    if(row < 0 || (size_t)row >= total_rows_per_term){
        res = -EINVARG;
        goto out;
    }
    terminal->text.row = row;
    terminal->text.col = col;
out:
    return res;
}

int terminal_cursor_row(struct terminal* terminal){
    return terminal->text.row;
}

int terminal_cursor_col(struct terminal* terminal){
    return terminal->text.col;
}

int terminal_total_cols(struct terminal* terminal){
    return terminal->bounds.width / terminal->font->bits_width_per_character;
}

int terminal_total_rows(struct terminal* terminal){
    return terminal->bounds.height / terminal->font->bits_height_per_character;
}

static bool terminal_bounds_check(struct terminal* terminal,
                                  size_t abs_x, size_t abs_y){
    size_t starting_x = terminal->bounds.abs_x;
    size_t starting_y = terminal->bounds.abs_y;
    size_t ending_x   = terminal->bounds.abs_x + terminal->bounds.width;
    size_t ending_y   = terminal->bounds.abs_y + terminal->bounds.height;
    return (abs_x >= starting_x && abs_x <= ending_x
         && abs_y >= starting_y && abs_y <= ending_y);
}

// Repaint a sub-rectangle of the terminal's saved background
// into the surface. Used by L98+ when overwriting characters.
//
// Upstream bug preserved: the bounds check compares abs_x and
// abs_y against bounds.width/height (sizes) where the intent
// is bounds.abs_x + width / abs_y + height (edges). Documented
// here; carried verbatim for diff hygiene.
// L98 references graphics_draw_rect, which lands in L99. Declare
// it here so terminal.o compiles; the link is satisfied by an
// L98-stub in graphics.c.
extern void graphics_draw_rect(struct graphics_info* g,
                               uint32_t abs_x, uint32_t abs_y,
                               size_t width, size_t height,
                               struct framebuffer_pixel color);

void terminal_restore_background(struct terminal* terminal,
                                 int sx, int sy,
                                 int width, int height){
    for(int x = 0; x < width; x++){
        for(int y = 0; y < height; y++){
            size_t abs_x = terminal->bounds.abs_x + sx + x;
            size_t abs_y = terminal->bounds.abs_y + sy + y;
            if(abs_x > terminal->bounds.width || abs_y > terminal->bounds.height){
                continue;
            }
            struct framebuffer_pixel background_pixel = {0};
            background_pixel = terminal->terminal_background[y * terminal->bounds.width + x];
            graphics_draw_pixel(terminal->graphics_info, abs_x, abs_y, background_pixel);
        }
    }
    size_t abs_x = terminal->bounds.abs_x + sx;
    size_t abs_y = terminal->bounds.abs_y + sy;
    graphics_redraw_graphics_to_screen(terminal->graphics_info,
                                       abs_x, abs_y, width, height);
}

// Lecture 98 - cursor-relative backspace. Moves the cursor back
// one cell (wrapping at column 0 to the previous row) and
// restores the cell's saved background, erasing whatever was
// drawn there.
int terminal_backspace(struct terminal* terminal){
    if(!(terminal->flags & TERMINAL_FLAG_BACKSPACE_ALLOWED)){
        return 0;
    }

    int total_rows  = terminal_total_rows(terminal);
    int total_cols  = terminal_total_cols(terminal);
    int current_col = terminal_cursor_col(terminal);
    int current_row = terminal_cursor_row(terminal);

    current_col--;
    if(current_col < 0){
        current_col = total_cols - 1;
        current_row--;
    }
    if(current_row < 0){
        current_row = total_rows - 1;
        current_col = 0;
    }
    if(current_col >= total_cols){
        current_col = 0;
        current_row++;
    }
    if(current_row >= total_rows){
        current_row = 0;
        current_col = 0;
    }

    terminal_cursor_set(terminal, current_row, current_col);

    size_t rel_x = terminal->text.col * terminal->font->bits_width_per_character;
    size_t rel_y = terminal->text.row * terminal->font->bits_height_per_character;
    terminal_restore_background(terminal, rel_x, rel_y,
                                terminal->font->bits_width_per_character,
                                terminal->font->bits_height_per_character);
    return 0;
}

// Single-character write. Handles `\n` and `0x08` (BS) specially;
// every other byte is drawn at the cursor and the cursor advances.
int terminal_write(struct terminal* terminal, int c){
    if(c == '\n'){
        terminal_handle_newline(terminal);
        return 0;
    }
    if(c == 0x08 && (terminal->flags & TERMINAL_FLAG_BACKSPACE_ALLOWED)){
        terminal_backspace(terminal);
        return 0;
    }

    size_t abs_x = terminal_abs_x_for_next_character(terminal);
    size_t abs_y = terminal_abs_y_for_next_character(terminal);
    font_draw(terminal->graphics_info, terminal->font, abs_x, abs_y, c,
              terminal->font_color);
    terminal_update_position_after_draw(terminal);
    return 0;
}

int terminal_pixel_set(struct terminal* terminal,
                       size_t x, size_t y,
                       struct framebuffer_pixel pixel_color){
    int res = 0;
    size_t abs_x = terminal->bounds.abs_x + x;
    size_t abs_y = terminal->bounds.abs_y + y;
    if(!terminal_bounds_check(terminal, abs_x, abs_y)){
        res = -EINVARG;
        goto out;
    }
    graphics_draw_pixel(terminal->graphics_info, abs_x, abs_y, pixel_color);
out:
    return res;
}

// Lecture 98 - stubs. L99 introduces the surface-level
// transparency/ignore-color setters; these forwarders will route
// to them once they land.
void terminal_transparency_key_set(struct terminal* terminal,
                                   struct framebuffer_pixel pixel_color){
    (void)terminal;
    (void)pixel_color;
    // graphics_transparency_key_set(terminal->graphics_info, pixel_color);
}

void terminal_transparency_key_remove(struct terminal* terminal){
    (void)terminal;
    // graphics_transparency_key_remove(terminal->graphics_info);
}

void terminal_ignore_color(struct terminal* terminal,
                           struct framebuffer_pixel pixel_color){
    (void)terminal;
    (void)pixel_color;
    // graphics_ignore_color(terminal->graphics_info, pixel_color);
}

void terminal_ignore_color_finish(struct terminal* terminal){
    (void)terminal;
    // graphics_ignore_color_finish(terminal->graphics_info);
}

int terminal_draw_image(struct terminal* terminal, uint32_t x, uint32_t y,
                        struct image* img){
    int res = 0;
    size_t abs_x = terminal->bounds.abs_x + x;
    size_t abs_y = terminal->bounds.abs_y + y;
    if(!terminal_bounds_check(terminal, abs_x, abs_y)){
        res = -EINVARG;
        goto out;
    }
    graphics_draw_image(terminal->graphics_info, img, abs_x, abs_y);
out:
    return res;
}

int terminal_draw_rect(struct terminal* terminal,
                       uint32_t x, uint32_t y,
                       size_t width, size_t height,
                       struct framebuffer_pixel pixel_color){
    int res = 0;
    size_t abs_x = terminal->bounds.abs_x + x;
    size_t abs_y = terminal->bounds.abs_y + y;
    if(!terminal_bounds_check(terminal, abs_x, abs_y)){
        res = -EINVARG;
        goto out;
    }
    graphics_draw_rect(terminal->graphics_info, abs_x, abs_y,
                       width, height, pixel_color);
out:
    return res;
}

int terminal_print(struct terminal* terminal, const char* message){
    int res = 0;
    while(*message != 0){
        res = terminal_write(terminal, *message);
        if(res < 0){
            break;
        }
        message++;
    }
    return res;
}
