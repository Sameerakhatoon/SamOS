#ifndef KERNEL_GRAPHICS_H
#define KERNEL_GRAPHICS_H
// Lecture 87 - graphics foundations. A graphics_info is a
// rectangular surface that owns a back buffer (`pixels`) and is
// composited onto a parent surface via redraw. The root
// graphics_info wraps the physical framebuffer that UEFI handed
// us in default_graphics_info (kernel.asm).
//
// Flags, transparency keying, child surfaces, and mouse hooks
// are stubbed at this commit; children are pushed but redraw
// does not yet walk them.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "lib/vector/vector.h"
#include "graphics/image/image.h"   // L90 - struct image for draw_image

enum {
    GRAPHICS_FLAG_ALLOW_OUT_OF_BOUNDS                = 0b00000001,
    GRAPHICS_FLAG_CLONED_FRAMEBUFFER                 = 0b00000010,
    GRAPHICS_FLAG_CLONED_CHILDREN                    = 0b00000100,
    GRAPHICS_FLAG_DO_NOT_COPY_PIXELS                 = 0b00001000,
    GRAPHICS_FLAG_DO_NOT_OVERWRITE_TRASPARENT_PIXELS = 0b00010000
};

struct graphics_info;

// NOTE: the int `type` slot is a placeholder for the upcoming
// MOUSE_CLICK_TYPE enum.
typedef void (*GRAPHICS_MOUSE_CLICK_FUNCTION)(struct graphics_info* graphics,
                                              size_t rel_x, size_t rel_y, int type);
typedef void (*GRAPHICS_MOUSE_MOVE_FUNCTION) (struct graphics_info* graphics,
                                              size_t rel_x, size_t rel_y,
                                              size_t abs_x, size_t abs_y);

struct framebuffer_pixel {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t reserved;
};

struct graphics_info {
    struct framebuffer_pixel* framebuffer;
    uint32_t                  horizontal_resolution;
    uint32_t                  vertical_resolution;
    uint32_t                  pixels_per_scanline;

    // Back buffer. `width*height*sizeof(framebuffer_pixel)`.
    // Writes go here; redraw composites into framebuffer.
    struct framebuffer_pixel* pixels;

    uint32_t                  width;
    uint32_t                  height;

    // Absolute position on the screen.
    uint32_t                  starting_x;
    uint32_t                  starting_y;

    // Position relative to parent.
    uint32_t                  relative_x;
    uint32_t                  relative_y;

    struct graphics_info*     parent;
    struct vector*            children;  // vector<struct graphics_info*>

    uint32_t                  flags;
    uint32_t                  z_index;   // higher = drawn later

    struct framebuffer_pixel  ignore_color;
    // Transparency key applies during redraw - any pixel matching
    // it shows what is behind. Black is the "no key" sentinel.
    struct framebuffer_pixel  transparency_key;

    struct {
        GRAPHICS_MOUSE_CLICK_FUNCTION mouse_click;
        GRAPHICS_MOUSE_MOVE_FUNCTION  mouse_move;
    } event_handlers;
};

void                  graphics_setup(struct graphics_info* main_graphics_info);
struct graphics_info* graphics_screen_info(void);
void                  graphics_draw_pixel(struct graphics_info* g, uint32_t x, uint32_t y,
                                          struct framebuffer_pixel pixel);
// Lecture 90 - paint a decoded image onto a surface, top-left
// corner at (x, y). NULL surface means the screen.
void                  graphics_draw_image(struct graphics_info* g,
                                          struct image* image, int x, int y);
// Lecture 98 - draw an axis-aligned filled rectangle. Real body
// landed in L99.
void                  graphics_draw_rect(struct graphics_info* g,
                                         uint32_t abs_x, uint32_t abs_y,
                                         size_t width, size_t height,
                                         struct framebuffer_pixel color);
// Lecture 99 - per-surface paint/composite-time colour filters.
void                  graphics_ignore_color(struct graphics_info* g,
                                            struct framebuffer_pixel pixel_color);
void                  graphics_ignore_color_finish(struct graphics_info* g);
void                  graphics_transparency_key_set(struct graphics_info* g,
                                                    struct framebuffer_pixel pixel_color);
void                  graphics_transparency_key_remove(struct graphics_info* g);
// Lecture 93 - regional redraw + screen-space wrapper.
void                  graphics_redraw_region(struct graphics_info* g,
                                             uint32_t local_x, uint32_t local_y,
                                             uint32_t width,   uint32_t height);
void                  graphics_redraw_graphics_to_screen(struct graphics_info* relative,
                                                         uint32_t rel_x, uint32_t rel_y,
                                                         uint32_t width, uint32_t height);
void                  graphics_redraw_children(struct graphics_info* g);
void                  graphics_redraw(struct graphics_info* g);
void                  graphics_redraw_all(void);

// Lecture 116 - essential helpers.
void                  graphics_set_z_index(struct graphics_info* graphics_info,
                                           uint32_t z_index);
struct graphics_info* graphics_info_create_relative(struct graphics_info* source_graphics,
                                                    size_t x, size_t y,
                                                    size_t width, size_t height,
                                                    int flags);
void                  graphics_paste_pixels_to_pixels(struct graphics_info* graphics_info_in,
                                                      struct graphics_info* graphics_info_out,
                                                      uint32_t src_x, uint32_t src_y,
                                                      uint32_t width, uint32_t height,
                                                      uint32_t dst_x, uint32_t dst_y,
                                                      int flags);
int                   graphics_pixel_get(struct graphics_info* graphics_info,
                                         uint32_t x, uint32_t y,
                                         struct framebuffer_pixel* pixel_out);
void                  graphics_info_free(struct graphics_info* graphics_in);
// Lecture 135 - recompute starting_x/y from relative_x/y +
// parent.starting_x/y, then recurse into children.
void                  graphics_info_recalculate(struct graphics_info* graphics_info);

#endif
