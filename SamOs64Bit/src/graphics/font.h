#ifndef KERNEL_FONT_H
#define KERNEL_FONT_H
// Lecture 92 - bitmap font foundation. A `struct font` holds the
// bit-packed glyph table for a fixed-pitch font; `font.c`
// (introduced this lecture, not yet linked) decodes a BMP into
// that table. L94 will fill in the API; L92 lands the shape
// only.
//
// Naming notes (preserved from upstream):
//   - `bits_height_per_chracter` is missing the trailing `a` in
//     "character"; we keep the typo so call sites can be diff-ed
//     against upstream without renames.
//   - `subtract_from_ascii_char_index_for_drawing` is the
//     literal upstream identifier.

#include <stdint.h>
#include <stddef.h>
#include "graphics/graphics.h"
#include "config.h"

#define FONT_IMAGE_DRAW_SUBTRACT_FROM_INDEX     32
#define FONT_IMAGE_CHARACTER_WIDTH_PIXEL_SIZE   9
#define FONT_IMAGE_CHRACTER_HEIGHT_PIXEL_SIZE   16
#define FONT_IMAGE_CHARACTER_Y_OFFSET           4

struct font {
    // ASCII range 0..255.
    size_t   character_count;

    // Packed bitmap: one bit per pixel, walked y * width + x.
    uint8_t* character_data;

    size_t   bits_width_per_character;
    size_t   bits_height_per_chracter;   // sic - upstream typo

    uint8_t  subtract_from_ascii_char_index_for_drawing;

    char     filename[SAMOS_MAX_PATH];
};

#endif
