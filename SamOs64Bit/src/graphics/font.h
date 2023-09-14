#ifndef KERNEL_FONT_H
#define KERNEL_FONT_H
// Lecture 92 + 94 - bitmap font subsystem.
//
// Identifier-typo notes preserved verbatim from upstream:
//   bits_height_per_character (missing the second `a` in
//   "character"), subtract_from_ascii_char_index_for_drawing.

#include <stdint.h>
#include <stddef.h>
#include "graphics/graphics.h"
#include "config.h"
#include "kernel.h"   // SAMOS_MAX_PATH

#define FONT_IMAGE_DRAW_SUBTRACT_FROM_INDEX     32
#define FONT_IMAGE_CHARACTER_WIDTH_PIXEL_SIZE   8   // Real 8x16 sheet (was a 9-wide stub)
#define FONT_IMAGE_CHRACTER_HEIGHT_PIXEL_SIZE   16
#define FONT_IMAGE_CHARACTER_Y_OFFSET           0   // Glyphs fill the whole cell

struct font {
    size_t   character_count;
    uint8_t* character_data;
    size_t   bits_width_per_character;
    // Lecture 97 - upstream finally renamed the field from the
    // `bits_height_per_chracter` typo to `bits_height_per_character`.
    size_t   bits_height_per_character;
    uint8_t  subtract_from_ascii_char_index_for_drawing;
    char     filename[SAMOS_MAX_PATH];
};

// Lecture 94 - public API.
int          font_system_init(void);
struct font* font_load(const char* filename);
struct font* font_get_loaded_font(const char* filename);
struct font* font_get_system_font(void);
struct font* font_create(uint8_t* character_data,
                         size_t  character_count,
                         size_t  bits_width_per_character,
                         size_t  bits_height_per_character,
                         uint8_t subtract_from_ascii_char_index_for_drawing);
int          font_draw(struct graphics_info* g, struct font* font,
                       int screen_x, int screen_y,
                       int character,
                       struct framebuffer_pixel font_color);
// Compiled-in 8x16 font; font_system_init falls back to it
// when @:/sysfont.bmp cannot be loaded.
#define FONT_BUILTIN_NAME "builtin:8x16"
struct font* font_builtin(void);
int          font_draw_text(struct graphics_info* g, struct font* font,
                            int screen_x, int screen_y,
                            const char* str,
                            struct framebuffer_pixel font_color);

#endif
