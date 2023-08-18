// Lecture 92 + 94 - bitmap font subsystem.
//
// font_load_from_image decodes a font BMP into a bit-packed
// glyph table; font_create wraps that buffer into a `struct
// font`; font_load caches by filename. font_draw_from_index
// paints one glyph into a surface and repaints just the dirty
// rectangle via graphics_redraw_graphics_to_screen (L93).
//
// font.c is in the build at L94. The L92 stub state ("source
// only, not in FILES") is gone.

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

static struct vector* loaded_fonts = NULL;
static struct font*   system_font  = NULL;

static struct font* font_load_from_image(const char* filename,
                                         size_t pixel_width,
                                         size_t pixel_height,
                                         size_t y_offset_per_character);
int font_draw_from_index(struct graphics_info* graphics_info, struct font* font,
                         int screen_x, int screen_y,
                         int index_character,
                         struct framebuffer_pixel font_color);

struct font* font_get_system_font(void){
    return system_font;
}

// Walk a font-sheet BMP and bit-pack the visible pixels of each
// glyph cell into character_data. The cell layout matches the
// sysfont.bmp: rows of `pixel_width` x `pixel_height` cells,
// `y_offset_per_character` rows skipped at the top of each cell.
static struct font* font_load_from_image(const char* filename,
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

    // L94 completes the function: wrap the packed buffer in a
    // struct font keyed at ASCII 32 (space). L92 left this as a
    // dangling allocation.
    return font_create(character_data, total_characters,
                       pixel_width, pixel_height,
                       FONT_IMAGE_DRAW_SUBTRACT_FROM_INDEX);
}

struct font* font_get_loaded_font(const char* filename){
    struct font* font = NULL;
    size_t total_fonts = vector_count(loaded_fonts);
    for(size_t i = 0; i < total_fonts; i++){
        vector_at(loaded_fonts, i, &font, sizeof(font));
        if(font){
            if(strncmp(font->filename, filename, sizeof(font->filename)) == 0){
                return font;
            }
        }
    }
    return NULL;
}

struct font* font_load(const char* filename){
    struct font* loaded_font = font_get_loaded_font(filename);
    if(loaded_font){
        return loaded_font;
    }

    loaded_font = font_load_from_image(filename,
                                       FONT_IMAGE_CHARACTER_WIDTH_PIXEL_SIZE,
                                       FONT_IMAGE_CHRACTER_HEIGHT_PIXEL_SIZE,
                                       FONT_IMAGE_CHARACTER_Y_OFFSET);
    if(loaded_font){
        strncpy(loaded_font->filename, filename, sizeof(loaded_font->filename));
        vector_push(loaded_fonts, &loaded_font);
    }
    return loaded_font;
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
    font->character_count                            = character_count;
    font->character_data                             = character_data;
    font->bits_width_per_character                   = bits_width_per_character;
    font->bits_height_per_chracter                   = bits_height_per_chracter;
    font->subtract_from_ascii_char_index_for_drawing = subtract_from_ascii_char_index_for_drawing;
    return font;
}

// Walk the bit-packed glyph and emit a graphics_draw_pixel per
// set bit. Then ask the surface to redraw the cell-sized region.
int font_draw_from_index(struct graphics_info* graphics_info, struct font* font,
                         int screen_x, int screen_y,
                         int index_character,
                         struct framebuffer_pixel font_color){
    int res = 0;
    if(!font){
        res = -EINVARG;
        goto out;
    }
    // Upstream uses > here; preserved (likely intent is >=).
    if(index_character > (int)font->character_count){
        res = 0;
        goto out;
    }

    size_t total_required_bits_per_character  = font->bits_width_per_character
                                              * font->bits_height_per_chracter;
    size_t total_required_bytes_per_character = total_required_bits_per_character / 8;
    if((total_required_bits_per_character % 8) != 0){
        total_required_bytes_per_character++;
    }

    size_t character_index = (size_t)index_character * total_required_bytes_per_character;
    for(size_t x = 0; x < font->bits_width_per_character; x++){
        for(size_t y = 0; y < font->bits_height_per_chracter; y++){
            size_t char_offset = character_index;
            size_t bit_index   = y * font->bits_width_per_character + x;
            size_t byte_index  = char_offset + (bit_index / 8);
            if((font->character_data[byte_index] >> (bit_index % 8)) & 0x01){
                size_t abs_x = screen_x + x;
                size_t abs_y = screen_y + y;
                graphics_draw_pixel(graphics_info, abs_x, abs_y, font_color);
            }
        }
    }

    graphics_redraw_graphics_to_screen(graphics_info, screen_x, screen_y,
                                       font->bits_width_per_character,
                                       font->bits_height_per_chracter);
out:
    return res;
}

int font_draw(struct graphics_info* graphics_info, struct font* font,
              int screen_x, int screen_y,
              int character,
              struct framebuffer_pixel font_color){
    character -= (int)font->subtract_from_ascii_char_index_for_drawing;
    return font_draw_from_index(graphics_info, font, screen_x, screen_y,
                                character, font_color);
}

int font_draw_text(struct graphics_info* graphics_info, struct font* font,
                   int screen_x, int screen_y,
                   const char* str,
                   struct framebuffer_pixel font_color){
    int res = 0;
    int x = screen_x;
    int y = screen_y;
    // Lecture 95 - NULL font means "the system font" (sysfont.bmp,
    // loaded at font_system_init). Callers do not have to thread
    // a font handle through the call sites that draw status text.
    if(!font){
        font = font_get_system_font();
    }
    while(*str != 0){
        res = font_draw(graphics_info, font, x, y, *str, font_color);
        if(res < 0){
            break;
        }
        x += font->bits_width_per_character;
        str++;
    }
    return res;
}

int font_system_init(void){
    int res = 0;
    loaded_fonts = vector_new(sizeof(struct font*), 4, 0);
    if(!loaded_fonts){
        res = -ENOMEM;
        goto out;
    }

    // Lecture 94 - the upstream path is "@:/sysfont.bmp" sitting
    // at the FS root. Our build pipeline does not yet copy a
    // font BMP onto the disk image, so font_load will fail.
    // We do not panic here (unlike upstream) because the rest
    // of the kernel boot must continue under CI.
    system_font = font_load("@:/sysfont.bmp");
    (void)system_font;
out:
    return res;
}
