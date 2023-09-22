// Lecture 119 - Window source part 1 (NOT YET LINKED).
//
// First slice of the window subsystem. Lands the module
// globals (windows vector, close-icon image, focus + drag
// state, autoincrement id), window_system_initialize +
// window_system_initialize_stage2, and the first half of
// window_create.
//
// Source-only at this lecture because:
//   - window_create falls off the end without a return; L120
//     finishes the body and adds `return window;`.
//   - L120-L123 add more code that this file references.
//
// We mirror upstream by NOT putting window.o in FILES yet.

#include "graphics/window.h"
#include "graphics/graphics.h"
#include "graphics/image/image.h"
#include "lib/vector/vector.h"
#include "memory/heap/kheap.h"
#include "keyboard/keyboard.h"
// L132+: PS/2 mouse header lands later.
#include "memory/memory.h"
#include "string/string.h"
#include "graphics/font.h"
#include "task/process.h"
// L128+: TSC delay header lands later.
#include "status.h"
#include "kernel.h"

// Vector of struct window* - every live window registers here.
struct vector* windows_vector = NULL;

// Loaded at boot from @:/clsicon.bmp; painted in the title bar.
struct image*  close_icon = NULL;

// Drag-state: the window currently being moved by the mouse.
struct window* window_moving = NULL;

// The window that has input focus.
struct window* focused_window = NULL;

// Auto-incrementing id stamped on windows that pass id == -1.
int window_autoincrement_id_current = 100000;

int window_system_initialize(void){
    int res = 0;

    windows_vector = vector_new(sizeof(struct window*), 10, 0);
    if(!windows_vector){
        res = -ENOMEM;
        goto out;
    }

    close_icon = graphics_image_load("@:/clsicon.bmp");
    if(!close_icon){
        res = -EIO;
        goto out;
    }

    window_moving                   = NULL;
    focused_window                  = NULL;
    window_autoincrement_id_current = 100000;
out:
    return res;
}

int window_system_initialize_stage2(void){
    // Lecture 119 - stage-2 hook. Mouse + keyboard listener
    // registration lands in L137+ / L175.
    return 0;
}

// Lecture 119 (part 1 of window_create).
//
// Upstream bug preserved verbatim: this function returns
// struct window* but falls off the end without ever returning
// it. The L120 commit appends the remaining layout work and a
// `return window;` at the bottom. We keep the open shape so the
// per-commit diff against upstream stays linear; the file is
// not yet in FILES so the missing return is inert.
struct window* window_create(struct graphics_info* graphics_info,
                             struct font* font,
                             const char* title,
                             size_t x, size_t y,
                             size_t width, size_t height,
                             int flags, int id){
    int res = 0;
    if(!windows_vector){
        panic("Window system was not initialized\n");
    }

    if(width < 1 || height < 1){
        res = -EINVARG;
        goto out;
    }

    if(!font){
        font = font_get_system_font();
    }

    struct window* window = kzalloc(sizeof(struct window));
    if(!window){
        res = -ENOMEM;
        goto out;
    }

    if(id == -1){
        id = window_autoincrement_id_current;
        window_autoincrement_id_current++;
    }

    strncpy(window->title, title, sizeof(window->title));
    window->x      = x;
    window->y      = y;
    window->width  = width;
    window->height = height;
    window->flags  = flags;
    window->id     = id;

    // Event handler vector. Holds WINDOW_EVENT_HANDLER fn ptrs
    // (NOT pointers to fn ptrs); the vector element_size is
    // therefore sizeof(WINDOW_EVENT_HANDLER), not pointer-to.
    window->event_handlers.handlers = vector_new(sizeof(WINDOW_EVENT_HANDLER), 4, 0);

    // Frame math. Borderless windows go straight to the body
    // dimensions; bordered windows expand by 2*border on x and
    // title_bar + border on y so the body still ends up at
    // (width x height) of usable pixels.
    size_t total_window_width_bounds  = width;
    size_t total_window_height_bounds = height;
    size_t window_body_height_offset  = 0;
    size_t window_body_width_offset   = 0;

    // L120 wires these up; declared here so the diff stays
    // close to upstream.
    struct graphics_info* title_bar_graphics_info     = NULL;
    struct graphics_info* border_left_graphics_info   = NULL;
    struct graphics_info* border_right_graphics_info  = NULL;
    struct graphics_info* border_bottom_graphics_info = NULL;
    (void)title_bar_graphics_info;
    (void)border_left_graphics_info;
    (void)border_right_graphics_info;
    (void)border_bottom_graphics_info;
    (void)window_body_height_offset;
    (void)window_body_width_offset;

    if(!(flags & WINDOW_FLAG_BORDERLESS)){
        total_window_width_bounds  += (WINDOW_BORDER_PIXEL_SIZE * 2);
        total_window_height_bounds += WINDOW_TITLE_BAR_HEIGHT + WINDOW_BORDER_PIXEL_SIZE;
        window_body_height_offset   = WINDOW_TITLE_BAR_HEIGHT;
        window_body_width_offset    = WINDOW_BORDER_PIXEL_SIZE;
    }

    struct graphics_info* root_graphics_info =
        graphics_info_create_relative(graphics_info, x, y,
                                      total_window_width_bounds,
                                      total_window_height_bounds,
                                      GRAPHICS_FLAG_DO_NOT_COPY_PIXELS);
    if(!root_graphics_info){
        res = -ENOMEM;
        goto out;
    }

    if(flags & WINDOW_FLAG_BACKGROUND_TRANSPARENT){
        // White (0xff, 0xff, 0xff, 0x00) becomes the transparency
        // key, then the whole surface is filled with it so the
        // default body is see-through.
        struct framebuffer_pixel transparency_key = {0};
        transparency_key.blue  = 0xff;
        transparency_key.green = 0xff;
        transparency_key.red   = 0xff;
        graphics_transparency_key_set(root_graphics_info, transparency_key);
        graphics_draw_rect(root_graphics_info, 0, 0,
                           root_graphics_info->width,
                           root_graphics_info->height,
                           transparency_key);
    }

    window->root_graphics = root_graphics_info;
out:
    (void)res;
    // L120 appends the title-bar + borders + return.
}
