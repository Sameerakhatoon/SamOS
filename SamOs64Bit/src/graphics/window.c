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

// L123+ forward decls; window.o lands in FILES at L122.

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

// Lecture 121 - paint the title bar: background fill, title
// text, and the close icon (white pixels ignored so the icon
// keys against the bar colour).
void window_draw_title_bar(struct window* window,
                           struct framebuffer_pixel title_bar_bg_color){
    if(!window || !window->title_bar_graphics){
        return;
    }

    size_t total_window_width_bounds = window->title_bar_graphics->width;
    size_t icon_pos_x = window->title_bar_components.close_btn.x;
    size_t icon_pos_y = window->title_bar_components.close_btn.y;
    const char* title = window->title;

    terminal_draw_rect(window->title_bar_terminal, 0, 0,
                       total_window_width_bounds, WINDOW_TITLE_BAR_HEIGHT,
                       title_bar_bg_color);

    terminal_cursor_set(window->title_bar_terminal, 0, 0);
    terminal_print(window->title_bar_terminal, title);

    struct framebuffer_pixel white_color = {0};
    white_color.red   = 0xff;
    white_color.green = 0xff;
    white_color.blue  = 0xff;
    terminal_ignore_color(window->title_bar_terminal, white_color);
    terminal_draw_image(window->title_bar_terminal, icon_pos_x, icon_pos_y, close_icon);
    terminal_ignore_color_finish(window->title_bar_terminal);
}

// Lecture 121 - vector_reorder comparator: higher zindex sorts
// later (front-most window drawn last).
int window_reorder(void* first_elem, void* second_elem){
    struct window* win1 = *(struct window**)(first_elem);
    struct window* win2 = *(struct window**)(second_elem);
    return (win1->zindex < win2->zindex);
}

// Lecture 121 - update the underlying graphics z and resort
// the windows vector so subsequent walks pick up the new
// ordering.
void window_set_z_index(struct window* window, int zindex){
    graphics_set_z_index(window->root_graphics, zindex);
    vector_reorder(windows_vector, window_reorder);
}

// Lecture 121 - flip a window out of focus: paint title bar
// black, redraw the screen region under the window.
void window_unfocus(struct window* old_focused_window){
    struct framebuffer_pixel black = {0};
    black.red   = 0x00;
    black.green = 0x00;
    black.blue  = 0x00;
    window_draw_title_bar(old_focused_window, black);
    graphics_redraw_region(graphics_screen_info(),
                           old_focused_window->root_graphics->starting_x,
                           old_focused_window->root_graphics->starting_y,
                           old_focused_window->root_graphics->width,
                           old_focused_window->root_graphics->height);
    // TODO L146 - emit WINDOW_EVENT_TYPE_LOST_FOCUS.
}

// Lecture 122 - bring a window to the top by giving it the
// highest z_index in the screen's children. Walks the last
// child (already the topmost after L121's sort) and stamps
// our window with `that.z_index + 1`.
void window_bring_to_top(struct window* window){
    size_t last_index = 0;
    struct graphics_info* screen_graphics = graphics_screen_info();
    size_t child_count = vector_count(screen_graphics->children);
    if(child_count > 0){
        struct graphics_info* child_graphics = NULL;
        size_t child_index = child_count - 1;
        vector_at(screen_graphics->children, child_index,
                  &child_graphics, sizeof(child_graphics));
        if(child_graphics){
            last_index = child_graphics->z_index;
        }
    }
    window_set_z_index(window, last_index + 1);
}

// Lecture 121 - focus a window: unfocus the previous, bring
// this one to the front (L122), repaint the title bar red,
// flush the root graphics region.
void window_focus(struct window* window){
    if(!window){
        return;
    }
    if(focused_window == window){
        return;
    }

    struct window* old_focused_window = focused_window;
    focused_window = window;

    struct framebuffer_pixel red = {0};
    red.red   = 0xff;
    red.green = 0x00;
    red.blue  = 0x00;

    if(old_focused_window && old_focused_window->title_bar_graphics){
        window_unfocus(old_focused_window);
    }

    window_bring_to_top(window);

    if(window->title_bar_graphics){
        window_draw_title_bar(window, red);
    }

    graphics_redraw_graphics_to_screen(window->root_graphics, 0, 0,
                                       window->root_graphics->width,
                                       window->root_graphics->height);
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

    // Lecture 120 - title bar + four borders + body surface +
    // terminals + close icon placement + initial paint.
    if(!(flags & WINDOW_FLAG_BORDERLESS)){
        title_bar_graphics_info =
            graphics_info_create_relative(root_graphics_info,
                                          WINDOW_BORDER_PIXEL_SIZE, 0,
                                          width, WINDOW_TITLE_BAR_HEIGHT, 0);
        if(!title_bar_graphics_info){
            res = -ENOMEM;
            goto out;
        }
        window->title_bar_graphics = title_bar_graphics_info;

        border_left_graphics_info =
            graphics_info_create_relative(root_graphics_info,
                                          0, WINDOW_TITLE_BAR_HEIGHT,
                                          WINDOW_BORDER_PIXEL_SIZE, height, 0);
        if(!border_left_graphics_info){
            res = -ENOMEM;
            goto out;
        }

        // Upstream shadow-declares border_right + border_bottom
        // inside the if-block, hiding the outer slots. The outer
        // pointers therefore stay NULL after the block exits.
        // We mirror verbatim; the bug is inert because window.o
        // is not linked yet.
        struct graphics_info* border_right_graphics_info =
            graphics_info_create_relative(root_graphics_info,
                                          total_window_width_bounds - WINDOW_BORDER_PIXEL_SIZE,
                                          WINDOW_TITLE_BAR_HEIGHT,
                                          WINDOW_BORDER_PIXEL_SIZE, height, 0);
        if(!border_right_graphics_info){
            res = -ENOMEM;
            goto out;
        }

        struct graphics_info* border_bottom_graphics_info =
            graphics_info_create_relative(root_graphics_info,
                                          0,
                                          total_window_height_bounds - WINDOW_BORDER_PIXEL_SIZE,
                                          width, WINDOW_BORDER_PIXEL_SIZE, 0);
        if(!border_bottom_graphics_info){
            res = -ENOMEM;
            goto out;
        }
        (void)border_right_graphics_info;
        (void)border_bottom_graphics_info;
    }

    struct graphics_info* window_graphics_info =
        graphics_info_create_relative(root_graphics_info,
                                      window_body_width_offset,
                                      window_body_height_offset,
                                      width, height, 0);
    if(!window_graphics_info){
        res = -ENOMEM;
        goto out;
    }
    window->graphics = window_graphics_info;

    if(!(flags & WINDOW_FLAG_BORDERLESS)){
        struct framebuffer_pixel title_bar_font_color = {0};
        title_bar_font_color.red   = 0xff;
        title_bar_font_color.blue  = 0xff;
        title_bar_font_color.green = 0xff;
        window->title_bar_terminal = terminal_create(title_bar_graphics_info,
                                                     0, 0,
                                                     total_window_width_bounds,
                                                     WINDOW_TITLE_BAR_HEIGHT,
                                                     font, title_bar_font_color, 0);
        if(!window->title_bar_terminal){
            res = -ENOMEM;
            goto out;
        }
    }

    struct framebuffer_pixel pixel_color = {0};
    pixel_color.red = 0xff;
    window->terminal = terminal_create(window_graphics_info, 0, 0,
                                       width, height, font, pixel_color,
                                       TERMINAL_FLAG_BACKSPACE_ALLOWED);
    if(!window->terminal){
        res = -ENOMEM;
        goto out;
    }

    struct framebuffer_pixel bg_color = {0};
    bg_color.red   = 0xff;
    bg_color.blue  = 0xff;
    bg_color.green = 0xff;
    terminal_draw_rect(window->terminal, 0, 0, width, height, bg_color);
    terminal_background_save(window->terminal);

    if(flags & WINDOW_FLAG_BACKGROUND_TRANSPARENT){
        terminal_transparency_key_set(window->terminal, bg_color);
    }

    if(!(flags & WINDOW_FLAG_BORDERLESS)){
        size_t icon_pos_x = window->title_bar_terminal->bounds.width
                          - close_icon->width
                          - (close_icon->width / 2);
        size_t icon_pos_y = (window->title_bar_terminal->bounds.height / 2)
                          - (close_icon->height / 2);
        window->title_bar_components.close_btn.x      = icon_pos_x;
        window->title_bar_components.close_btn.y      = icon_pos_y;
        window->title_bar_components.close_btn.width  = close_icon->width;
        window->title_bar_components.close_btn.height = close_icon->height;

        struct framebuffer_pixel title_bar_bg_color = {0};
        window_draw_title_bar(window, title_bar_bg_color);

        // Upstream bug preserved: each of these three draws hits
        // border_left_graphics_info because the outer right /
        // bottom pointers are still NULL (shadow-decl scoping
        // above). Carried verbatim.
        struct framebuffer_pixel border_color = {0};
        graphics_draw_rect(border_left_graphics_info, 0, 0,
                           border_left_graphics_info->width,
                           border_left_graphics_info->height,
                           border_color);
        graphics_draw_rect(border_left_graphics_info, 0, 0,
                           border_left_graphics_info->width,
                           border_left_graphics_info->height,
                           border_color);
        graphics_draw_rect(border_left_graphics_info, 0, 0,
                           border_left_graphics_info->width,
                           border_left_graphics_info->height,
                           border_color);
    }

    vector_push(windows_vector, &window);

    size_t child_count = vector_count(window->root_graphics->children);
    window_set_z_index(window, child_count + 1);

    // L121+ - register window event handler.

    window_focus(window);
    graphics_redraw_all();

out:
    if(res < 0){
        if(window){
            if(window->terminal){
                terminal_free(window->terminal);
                window->terminal = NULL;
            }
            if(window->title_bar_terminal){
                terminal_free(window->title_bar_terminal);
                window->title_bar_terminal = NULL;
            }
            vector_pop_element(windows_vector, &window, sizeof(struct window*));
            kfree(window);
            window = NULL;
        }
    }
    return window;
}
