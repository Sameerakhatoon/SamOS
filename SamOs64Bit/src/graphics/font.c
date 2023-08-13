// Lecture 92 - bitmap font foundation (NOT YET LINKED).
//
// font.c lands as source-only in upstream L92: the file is added
// but Makefile FILES is not updated. We follow the same pattern.
// Inspecting it now is helpful because the upstream code has
// three open issues that subsequent lectures resolve:
//
//   1. font_load_from_image is missing a `return` for its
//      computed character_data (and the wrapper struct it would
//      build).
//   2. font_load (referenced by font_system_init) is never
//      defined.
//   3. graphics_redraw_graphics_to_screen is called by
//      font_draw_from_index but does not exist yet.
//
// L93 adds the redraw-region helper. L94 fills in the loader
// wrapper. Until then this file is reference material only and
// pulling it into the build would break the link.
//
// We keep the upstream identifier typos
// (bits_height_per_chracter) verbatim.

#include "graphics/font.h"
#include "graphics/graphics.h"
#include "graphics/image/image.h"
#include "memory/heap/kheap.h"
#include "lib/vector/vector.h"
#include "memory/memory.h"
#include "string/string.h"
#include "kernel.h"
#include "status.h"
#include <stdbool.h>

struct vector* loaded_fonts = NULL;
struct font*   system_font  = NULL;

struct font* font_get_system_font(void){
    return system_font;
}

// L92 walks the font BMP, samples every per-character box, and
// flips bits in `character_data` for non-black source pixels.
// Returning the populated struct font is L94's job; the upstream
// commit leaves the function without a return path.
struct font* font_load_from_image(const char* filename,
                                  size_t pixel_width,
                                  size_t pixel_height,
                                  size_t y_offset_per_character){
    struct image* img_font = graphics_image_load(filename);
    if(!img_font){
        return NULL;
    }

    size_t characters_per_row = img_font->width  / pixel_width;
    size_t total_rows         = img_font->height / pixel_height;
    size_t total_characters   = total_rows * characters_per_row;
    if(total_characters > 255){
        total_characters = 255;
    }

    size_t total_required_bits_for_character_set =
        total_characters * pixel_width * pixel_height;
    size_t total_required_bytes_for_character_set =
        total_required_bits_for_character_set / 8;
    if((total_required_bits_for_character_set % 8) != 0){
        total_required_bytes_for_character_set++;
    }

    size_t total_required_bits_per_character  = pixel_width * pixel_height;
    size_t total_required_bytes_per_character = total_required_bits_per_character / 8;
    if((total_required_bits_per_character % 8) != 0){
        total_required_bytes_per_character++;
    }

    uint8_t* character_data = kzalloc(total_required_bytes_for_character_set);
    if(!character_data){
        return NULL;
    }

    for(size_t row = 0; row < total_rows; row++){
        for(size_t col = 0; col < characters_per_row; col++){
            size_t character_index = row * characters_per_row + col;
            size_t starting_x      = col * pixel_width;
            size_t starting_y      = row * pixel_height;

            for(size_t x = 0; x < pixel_width; x++){
                for(size_t y = y_offset_per_character; y < pixel_height; y++){
                    size_t abs_x = starting_x + x;
                    size_t abs_y = starting_y + y;
                    image_pixel_data pixel = graphics_image_get_pixel(img_font, abs_x, abs_y);
                    if(pixel.R != 0 || pixel.B != 0 || pixel.G != 0){
                        // Non-black source pixel = bit set.
                        size_t char_offset = character_index * total_required_bytes_per_character;
                        size_t bit_index   = y * pixel_width + x;
                        size_t byte_index  = char_offset + (bit_index / 8);
                        uint8_t bit_mask   = 1 << (bit_index % 8);
                        character_data[byte_index] |= bit_mask;
                    }
                }
            }
        }
    }

    // L94 will add the font_create call here and `return font`.
    // For L92 the function falls off the end, which is why the
    // file is not in the build.
    (void)character_data;
    return NULL;
}

struct font* font_create(uint8_t* character_data,
                         size_t  character_count,
                         size_t  bits_width_per_character,
                         size_t  bits_height_per_chracter,
                         uint8_t subtract_from_ascii_char_index_for_drawing){
    struct font* font = kzalloc(sizeof(struct font));
    if(!font){
        return NULL;
    }
    font->character_count                              = character_count;
    font->character_data                               = character_data;
    font->bits_width_per_character                     = bits_width_per_character;
    font->bits_height_per_chracter                     = bits_height_per_chracter;
    font->subtract_from_ascii_char_index_for_drawing   = subtract_from_ascii_char_index_for_drawing;
    return font;
}
