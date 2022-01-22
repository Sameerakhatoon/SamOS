// Lecture 87 - graphics foundation. Implements
// graphics_setup/draw/redraw on top of the UEFI-supplied
// framebuffer. The root graphics_info (default_graphics_info)
// lives in kernel.asm so the long-mode entry can stash the
// UEFI register payload directly into it.

#include "graphics.h"
#include "graphics/image/image.h"
#include "graphics/window.h"   // L141 - struct window for click hit-test
#include "kernel.h"
#include "memory/paging/paging.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "lib/vector/vector.h"
#include "status.h"

static struct graphics_info* loaded_graphics_info  = NULL;

// Every graphics_info ever registered. The root is index 0; child
// surfaces will land here as the system grows.
static struct vector*        graphics_info_vector  = NULL;

static void*  real_framebuffer                     = NULL;
static void*  real_framebuffer_end                 = NULL;
static size_t real_framebuffer_width               = 0;
static size_t real_framebuffer_height              = 0;
static size_t real_framebuffer_pixels_per_scanline = 0;

// Lecture 116 - forward decls for the new child-management
// helpers landing this lecture.
void graphics_info_children_free(struct graphics_info* graphics_info);

// Lecture 116 - bounds predicate against a surface's own
// (width, height). Used by graphics_pixel_get and any future
// safe-read path.
bool graphics_bounds_check(struct graphics_info* graphics_info, int x, int y){
    return !(x < 0 || y < 0
          || x >= (int)graphics_info->width
          || y >= (int)graphics_info->height);
}

struct graphics_info* graphics_screen_info(void){
    return loaded_graphics_info;
}

// L141 forward decls so graphics_mouse_click_handler can call
// the helpers defined later in this file.
struct graphics_info* graphics_get_at_screen_position(size_t x, size_t y,
                                                     struct graphics_info* ignored,
                                                     bool top_first);

// Lecture 141 - bubble a click up the surface tree. If the
// surface has a click handler installed, fire it. Otherwise
// adjust the coords to parent-relative and recurse.
void graphics_mouse_click(struct graphics_info* graphics,
                          size_t rel_x_clicked, size_t rel_y_clicked,
                          MOUSE_CLICK_TYPE type){
    if(graphics->event_handlers.mouse_click){
        graphics->event_handlers.mouse_click(graphics, rel_x_clicked, rel_y_clicked, type);
        return;
    }
    if(graphics->parent){
        graphics_mouse_click(graphics->parent,
                             graphics->relative_x + rel_x_clicked,
                             graphics->relative_y + rel_y_clicked,
                             type);
    }
}

// Lecture 141 - mouse-click event handler registered with the
// mouse subsystem. Resolves the click coordinate to a leaf
// graphics surface (skipping the mouse window's own subtree
// so the cursor sprite never gets clicked) and bubbles up.
void graphics_mouse_click_handler(struct mouse* mouse, int clicked_x, int clicked_y,
                                  MOUSE_CLICK_TYPE type){
    struct graphics_info* graphics =
        graphics_get_at_screen_position(clicked_x, clicked_y,
                                        mouse->graphic.window->root_graphics, true);
    if(graphics){
        if(clicked_x < (int)graphics->starting_x
           || clicked_y < (int)graphics->starting_y){
            return;
        }
        size_t rel_x = clicked_x - graphics->starting_x;
        size_t rel_y = clicked_y - graphics->starting_y;
        if(graphics_bounds_check(graphics, rel_x, rel_y)){
            graphics_mouse_click(graphics, rel_x, rel_y, type);
        }
    }
}

// Copy a (clipped) rectangle from `src_info->pixels` to the
// physical framebuffer at (dst_abs_x, dst_abs_y). Skips pixels
// matching src_info->transparency_key. Black-key means "no key".
// L93 promoted to file-scope (no `static`) so the new
// graphics_redraw_region in this file can reuse it.
void graphics_paste_pixels_to_framebuffer(struct graphics_info* src_info,
                                                 uint32_t src_x, uint32_t src_y,
                                                 uint32_t width, uint32_t height,
                                                 uint32_t dst_abs_x, uint32_t dst_abs_y){
    if(!src_info){
        return;
    }

    uint32_t src_x_end = src_x + width;
    uint32_t src_y_end = src_y + height;
    if(src_x_end > src_info->width){
        src_x_end = src_info->width;
    }
    if(src_y_end > src_info->height){
        src_y_end = src_info->height;
    }

    uint32_t final_w = src_x_end - src_x;
    uint32_t final_h = src_y_end - src_y;
    if(final_w == 0 || final_h == 0){
        return;
    }

    struct graphics_info* screen = graphics_screen_info();
    uint32_t screen_w = screen->horizontal_resolution;
    uint32_t screen_h = screen->vertical_resolution;

    if(dst_abs_x >= screen_w || dst_abs_y >= screen_h){
        return;
    }

    uint32_t dst_x_end = dst_abs_x + final_w;
    uint32_t dst_y_end = dst_abs_y + final_h;
    if(dst_x_end > screen_w){
        dst_x_end = screen_w;
    }
    if(dst_y_end > screen_h){
        dst_y_end = screen_h;
    }

    uint32_t clipped_w = dst_x_end - dst_abs_x;
    uint32_t clipped_h = dst_y_end - dst_abs_y;
    if(clipped_w == 0 || clipped_h == 0){
        return;
    }

    struct framebuffer_pixel no_transparency_color = {0};
    bool have_key = memcmp(&src_info->transparency_key, &no_transparency_color,
                           sizeof(no_transparency_color)) != 0;

    for(uint32_t ly = 0; ly < clipped_h; ly++){
        for(uint32_t lx = 0; lx < clipped_w; lx++){
            struct framebuffer_pixel p =
                src_info->pixels[(src_y + ly) * src_info->width + (src_x + lx)];

            if(have_key &&
               memcmp(&p, &src_info->transparency_key, sizeof(p)) == 0){
                continue;
            }

            screen->framebuffer[(dst_abs_y + ly) * screen->pixels_per_scanline
                                + (dst_abs_x + lx)] = p;
        }
    }
}

void graphics_draw_pixel(struct graphics_info* graphics_info, uint32_t x, uint32_t y,
                         struct framebuffer_pixel pixel){
    struct framebuffer_pixel black_pixel = {0};

    // ignore_color = black means "do not ignore anything".
    if(memcmp(&graphics_info->ignore_color, &black_pixel,
              sizeof(graphics_info->ignore_color)) != 0){
        if(memcmp(&graphics_info->ignore_color, &pixel,
                  sizeof(graphics_info->ignore_color)) == 0){
            return;
        }
    }

    if(x < graphics_info->width && y < graphics_info->height){
        graphics_info->pixels[y * graphics_info->width + x] = pixel;
    }
}

// Lecture 99 - replace the L98 stub with the real rect filler.
// Walks the rectangle column-major and calls graphics_draw_pixel
// per cell so ignore_color and bounds checks still apply.
void graphics_draw_rect(struct graphics_info* graphics_info,
                        uint32_t x, uint32_t y,
                        size_t width, size_t height,
                        struct framebuffer_pixel pixel_color){
    uint32_t x_end = x + (uint32_t)width;
    uint32_t y_end = y + (uint32_t)height;
    for(uint32_t lx = x; lx < x_end; lx++){
        for(uint32_t ly = y; ly < y_end; ly++){
            graphics_draw_pixel(graphics_info, lx, ly, pixel_color);
        }
    }
}

// Lecture 135 - propagate the parent's screen position into
// this surface's `starting_x/y` and recurse so every
// descendant lands at the right absolute coordinate. Called
// after a window move (L134) updates the root's relative
// coords.
void graphics_info_recalculate(struct graphics_info* graphics_info){
    if(graphics_info->parent){
        graphics_info->starting_x =
            graphics_info->relative_x + graphics_info->parent->starting_x;
        graphics_info->starting_y =
            graphics_info->relative_y + graphics_info->parent->starting_y;
    }
    if(graphics_info->children){
        size_t total_children = vector_count(graphics_info->children);
        for(size_t i = 0; i < total_children; i++){
            struct graphics_info* child_graphics_info = NULL;
            int res = vector_at(graphics_info->children, i,
                                &child_graphics_info,
                                sizeof(struct graphics_info*));
            if(res < 0){
                break;
            }
            graphics_info_recalculate(child_graphics_info);
        }
    }
}

// Lecture 99 - paint-time pixel-skip filter. Stash a colour the
// renderer should treat as "do not write this pixel". Black is
// the "no filter" sentinel; black sources still draw because
// the equality check matches the field, not the pixel.
void graphics_ignore_color(struct graphics_info* graphics_info,
                           struct framebuffer_pixel pixel_color){
    graphics_info->ignore_color = pixel_color;
}

// Lecture 99 - composite-time transparency key.
//
// Upstream bug preserved: this function ALSO writes
// `ignore_color` rather than `transparency_key`. They are
// different semantic slots (ignore_color is paint-time;
// transparency_key is composite-time). Mirroring upstream
// keeps the diff readable; a follow-up patch can split them.
void graphics_transparency_key_set(struct graphics_info* graphics_info,
                                   struct framebuffer_pixel pixel_color){
    graphics_info->ignore_color = pixel_color;
}

// Lecture 99 - clear the composite-time transparency key. The
// upstream form resets `transparency_key` (not `ignore_color`)
// to black, so a paired set/remove leaves ignore_color polluted.
// The bug is preserved.
void graphics_transparency_key_remove(struct graphics_info* graphics_info){
    struct framebuffer_pixel pixel_black = {0};
    graphics_info->transparency_key = pixel_black;
}

// Lecture 99 - clear the paint-time ignore filter.
void graphics_ignore_color_finish(struct graphics_info* graphics_info){
    struct framebuffer_pixel black_color = {0};
    graphics_info->ignore_color = black_color;
}

// Lecture 90 - composite a decoded image into a surface's back
// buffer at (x, y). NULL surface means the screen root.
// Iterates pixels through graphics_draw_pixel so the surface's
// ignore_color logic and bounds check apply uniformly.
void graphics_draw_image(struct graphics_info* graphics_info,
                         struct image* image, int x, int y){
    if(!image){
        return;
    }
    if(!graphics_info){
        graphics_info = loaded_graphics_info;
    }

    for(size_t lx = 0; lx < (size_t)image->width; lx++){
        for(size_t ly = 0; ly < (size_t)image->height; ly++){
            size_t sx = x + lx;
            size_t sy = y + ly;

            image_pixel_data* pixel_data =
                &((image_pixel_data*)image->data)[ly * image->width + lx];

            struct framebuffer_pixel fb_pixel = {0};
            fb_pixel.red   = pixel_data->R;
            fb_pixel.green = pixel_data->G;
            fb_pixel.blue  = pixel_data->B;

            graphics_draw_pixel(graphics_info, sx, sy, fb_pixel);
        }
    }
}

static void graphics_redraw_only(struct graphics_info* g){
    if(!g){
        return;
    }
    graphics_paste_pixels_to_framebuffer(g, 0, 0, g->width, g->height,
                                         g->starting_x, g->starting_y);
}

// Lecture 93 - walk a surface's `children` vector and redraw
// each. Recursive: each child's redraw calls back here for its
// own grandchildren.
void graphics_redraw_children(struct graphics_info* g){
    size_t total_children = vector_count(g->children);
    for(size_t i = 0; i < total_children; i++){
        struct graphics_info* child = NULL;
        vector_at(g->children, i, &child, sizeof(child));
        if(child){
            graphics_redraw(child);
        }
    }
}

// Lecture 93 - paint only a sub-rectangle of `g` onto the
// framebuffer, then redraw the parts of every child that overlap
// the rectangle. Used by the font path (small per-glyph redraws)
// and by L99's transparency work.
//
// Upstream bug preserved (NOT corrected): intersect_right is
// computed with MAX, not MIN. That makes the right edge always
// extend at least as far as region_abs_right, which is wrong for
// children that end before the redraw region. We keep the
// upstream form so the diff lines up; a future commit can fix it.
void graphics_redraw_region(struct graphics_info* g,
                            uint32_t local_x, uint32_t local_y,
                            uint32_t width,   uint32_t height){
    if(!g){
        return;
    }
    if(local_x >= g->width || local_y >= g->height){
        return;
    }
    if(local_x + width > g->width){
        return;
    }
    if(local_y + height > g->height){
        height = g->height - local_y;
    }

    uint32_t dst_abs_x = g->starting_x + local_x;
    uint32_t dst_abs_y = g->starting_y + local_y;

    graphics_paste_pixels_to_framebuffer(g, local_x, local_y,
                                         width, height,
                                         dst_abs_x, dst_abs_y);

    uint32_t region_abs_left   = dst_abs_x;
    uint32_t region_abs_top    = dst_abs_y;
    uint32_t region_abs_right  = dst_abs_x + width;
    uint32_t region_abs_bottom = dst_abs_y + height;

    size_t child_count = vector_count(g->children);
    for(size_t i = 0; i < child_count; i++){
        struct graphics_info* child = NULL;
        vector_at(g->children, i, &child, sizeof(child));
        if(!child){
            continue;
        }

        uint32_t child_abs_left   = child->starting_x;
        uint32_t child_abs_top    = child->starting_y;
        uint32_t child_abs_right  = child->starting_x + child->width;
        uint32_t child_abs_bottom = child->starting_y + child->height;

        uint32_t intersect_left   = MAX(child_abs_left,   region_abs_left);
        uint32_t intersect_top    = MAX(child_abs_top,    region_abs_top);
        // L93 upstream bug preserved: should be MIN.
        uint32_t intersect_right  = MAX(child_abs_right,  region_abs_right);
        uint32_t intersect_bottom = MIN(child_abs_bottom, region_abs_bottom);

        if(intersect_right > intersect_left && intersect_bottom > intersect_top){
            uint32_t child_local_x    = intersect_left - child_abs_left;
            uint32_t child_local_y    = intersect_top  - child_abs_top;
            uint32_t intersect_width  = intersect_right  - intersect_left;
            uint32_t intersect_height = intersect_bottom - intersect_top;
            graphics_redraw_region(child, child_local_x, child_local_y,
                                   intersect_width, intersect_height);
        }
    }
}

// Lecture 93 - convenience helper: project a `relative_graphics`
// rectangle to absolute screen coords and redraw that region of
// the root. Used by font_draw_from_index in L94+.
void graphics_redraw_graphics_to_screen(struct graphics_info* relative_graphics,
                                        uint32_t rel_x, uint32_t rel_y,
                                        uint32_t width, uint32_t height){
    uint32_t abs_screen_x = relative_graphics->starting_x + rel_x;
    uint32_t abs_screen_y = relative_graphics->starting_y + rel_y;
    graphics_redraw_region(graphics_screen_info(),
                           abs_screen_x, abs_screen_y, width, height);
}

// Lecture 141 - public setter to wire a per-surface click
// handler. Used by L143+ window code to handle clicks against
// title bar / body / close button.
void graphics_click_handler_set(struct graphics_info* graphics,
                                GRAPHICS_MOUSE_CLICK_FUNCTION click_function){
    graphics->event_handlers.mouse_click = click_function;
}

// Lecture 141 - is `elem` (or any of its ancestors) the
// `ignored` subtree root? Used by the click hit-test to skip
// the mouse-cursor window.
bool graphics_is_in_ignored_branch(struct graphics_info* elem,
                                   struct graphics_info* ignored){
    if(!ignored){
        return false;
    }
    for(struct graphics_info* cur = elem; cur != NULL; cur = cur->parent){
        if(cur == ignored){
            return true;
        }
    }
    return false;
}

// Lecture 141 - find the leaf surface containing the point
// (x, y), skipping `ignored`. `top_first` flips the children
// iteration so the front-most window wins.
//
// Upstream bugs preserved verbatim:
//   - the fallback `return graphics` check ANDs
//     `x < graphics->starting_y + graphics->width` (should be
//     `+ graphics->width`, but compares against
//     `starting_y`). Almost always wrong.
//   - top_first uses `x >=`, top_first=false uses `x >` for
//     the left edge. Inconsistent.
struct graphics_info* graphics_get_child_at_position(struct graphics_info* graphics,
                                                    size_t x, size_t y,
                                                    struct graphics_info* ignored,
                                                    bool top_first){
    if(graphics_is_in_ignored_branch(graphics, ignored)){
        return NULL;
    }

    size_t total = vector_count(graphics->children);
    struct graphics_info* result = NULL;
    if(top_first){
        for(size_t i = total; i > 0; i--){
            size_t index = i - 1;
            struct graphics_info* child = NULL;
            vector_at(graphics->children, index, &child, sizeof(child));
            if(!child){
                continue;
            }
            if(graphics_is_in_ignored_branch(child, ignored)){
                continue;
            }
            if(x >= child->starting_x && x < child->starting_x + child->width
               && y >= child->starting_y && y < child->starting_y + child->height){
                result = graphics_get_child_at_position(child, x, y, ignored, top_first);
                if(result){
                    return result;
                }
                return child;
            }
        }
    }else{
        for(size_t i = 0; i < total; i++){
            struct graphics_info* child = NULL;
            vector_at(graphics->children, i, &child, sizeof(child));
            if(!child){
                continue;
            }
            if(graphics_is_in_ignored_branch(child, ignored)){
                continue;
            }
            if(x > child->starting_x && x < child->starting_x + child->width
               && y >= child->starting_y && y < child->starting_y + child->height){
                result = graphics_get_child_at_position(child, x, y, ignored, top_first);
                if(result){
                    return result;
                }
                return child;
            }
        }
    }

    // Upstream bug preserved: the right-edge check uses
    // `starting_y + graphics->width` rather than
    // `starting_x + graphics->width`. Carried verbatim.
    if(x >= graphics->starting_x
       && x < graphics->starting_y + graphics->width
       && y >= graphics->starting_y
       && y < graphics->starting_y + graphics->height){
        return graphics;
    }
    return NULL;
}

struct graphics_info* graphics_get_at_screen_position(size_t x, size_t y,
                                                     struct graphics_info* ignored,
                                                     bool top_first){
    return graphics_get_child_at_position(graphics_screen_info(),
                                          x, y, ignored, top_first);
}

void graphics_redraw(struct graphics_info* g){
    if(!g){
        return;
    }
    graphics_redraw_only(g);
    graphics_redraw_children(g);
}

void graphics_redraw_all(void){
    graphics_redraw(graphics_screen_info());
}

// Lecture 116 - safe pixel read. Returns -EINVARG when (x, y)
// is out of bounds; fills *pixel_out otherwise.
int graphics_pixel_get(struct graphics_info* graphics_info, uint32_t x, uint32_t y,
                       struct framebuffer_pixel* pixel_out){
    int res = 0;
    if(!graphics_bounds_check(graphics_info, (int)x, (int)y)){
        res = -EINVARG;
        goto out;
    }
    *pixel_out = graphics_info->pixels[y * graphics_info->width + x];
out:
    return res;
}

// Lecture 116 - vector_reorder comparator. Sorts children so
// higher z_index sits later in the vector. Returns > 0 when
// `first` should come AFTER `second`.
int graphics_reorder(void* first_element, void* second_element){
    struct graphics_info* first_graphics  = *((struct graphics_info**)first_element);
    struct graphics_info* second_graphics = *((struct graphics_info**)second_element);
    return first_graphics->z_index > second_graphics->z_index;
}

// Lecture 116 - update a surface's z and reorder the parent's
// children so the compositor walks back-to-front correctly.
void graphics_set_z_index(struct graphics_info* graphics_info, uint32_t z_index){
    graphics_info->z_index = z_index;
    if(graphics_info->parent && graphics_info->parent->children){
        vector_reorder(graphics_info->parent->children, graphics_reorder);
    }
}

// Lecture 116 - surface-to-surface paste. Source pixels go
// through graphics_pixel_get so bounds checks apply on read;
// the destination respects its own transparency_key when the
// caller passes GRAPHICS_FLAG_DO_NOT_OVERWRITE_TRASPARENT_PIXELS
// (sic: upstream typo "TRASPARENT" preserved).
void graphics_paste_pixels_to_pixels(struct graphics_info* graphics_info_in,
                                     struct graphics_info* graphics_info_out,
                                     uint32_t src_x, uint32_t src_y,
                                     uint32_t width, uint32_t height,
                                     uint32_t dst_x, uint32_t dst_y,
                                     int flags){
    uint32_t src_x_end = src_x + width;
    uint32_t src_y_end = src_y + height;
    if(src_x_end > graphics_info_in->width){
        src_x_end = graphics_info_in->width;
    }
    if(src_y_end > graphics_info_in->height){
        src_y_end = graphics_info_in->height;
    }
    uint32_t final_w = src_x_end - src_x;
    uint32_t final_h = src_y_end - src_y;

    bool has_transparency_key = false;
    struct framebuffer_pixel ignore_transparent_pixel = {0};
    if(memcmp(&graphics_info_out->transparency_key,
              &ignore_transparent_pixel,
              sizeof(graphics_info_out->transparency_key)) != 0){
        has_transparency_key = true;
    }

    for(uint32_t lx = 0; lx < final_w; lx++){
        for(uint32_t ly = 0; ly < final_h; ly++){
            struct framebuffer_pixel pixel = {0};
            graphics_pixel_get(graphics_info_in, src_x + lx, src_y + ly, &pixel);

            uint32_t dx = dst_x + lx;
            uint32_t dy = dst_y + ly;

            if(dx < graphics_info_out->width && dy < graphics_info_out->height){
                if(flags & GRAPHICS_FLAG_DO_NOT_OVERWRITE_TRASPARENT_PIXELS){
                    if(has_transparency_key){
                        struct framebuffer_pixel* existing_pixel =
                            &graphics_info_out->pixels[dy * graphics_info_out->width + dx];
                        if(memcmp(existing_pixel,
                                  &graphics_info_out->transparency_key,
                                  sizeof(struct framebuffer_pixel)) == 0){
                            continue;
                        }
                    }
                }
            }
            graphics_info_out->pixels[dy * graphics_info_out->width + dx] = pixel;
        }
    }
}

// Lecture 116 - build a child surface as a sub-rectangle of an
// existing surface. The child inherits the parent framebuffer
// and registers itself in parent->children. With
// GRAPHICS_FLAG_DO_NOT_COPY_PIXELS the new pixel buffer stays
// zeroed; otherwise the parent's pixels under the rectangle
// are pasted in.
struct graphics_info* graphics_info_create_relative(struct graphics_info* source_graphics,
                                                    size_t x, size_t y,
                                                    size_t width, size_t height,
                                                    int flags){
    int res = 0;
    struct graphics_info* new_graphics = NULL;
    if(source_graphics == NULL){
        panic("Source graphics null\n");
        res = -EINVARG;
        goto out;
    }

    new_graphics = kzalloc(sizeof(struct graphics_info));
    if(!new_graphics){
        res = -ENOMEM;
        goto out;
    }

    size_t parent_x      = source_graphics->starting_x;
    size_t parent_y      = source_graphics->starting_y;
    size_t parent_width  = source_graphics->horizontal_resolution;
    size_t parent_height = source_graphics->vertical_resolution;

    size_t starting_x = parent_x + x;
    size_t starting_y = parent_y + y;
    size_t ending_x   = starting_x + width;
    size_t ending_y   = starting_y + height;

    if(!(flags & GRAPHICS_FLAG_ALLOW_OUT_OF_BOUNDS)){
        if(ending_x > parent_x + parent_width || ending_y > parent_y + parent_height){
            res = -EINVARG;
            goto out;
        }
    }

    new_graphics->horizontal_resolution = source_graphics->horizontal_resolution;
    new_graphics->vertical_resolution   = source_graphics->vertical_resolution;
    new_graphics->pixels_per_scanline   = source_graphics->pixels_per_scanline;
    new_graphics->width                 = width;
    new_graphics->height                = height;
    new_graphics->starting_x            = starting_x;
    new_graphics->starting_y            = starting_y;
    new_graphics->relative_x            = x;
    new_graphics->relative_y            = y;
    new_graphics->framebuffer           = source_graphics->framebuffer;
    new_graphics->parent                = source_graphics;
    new_graphics->children              = vector_new(sizeof(struct graphics_info*), 4, 0);
    new_graphics->pixels                = kzalloc(new_graphics->width
                                               * new_graphics->height
                                               * sizeof(struct framebuffer_pixel));
    if(!new_graphics->pixels){
        res = -ENOMEM;
        goto out;
    }

    if(!(flags & GRAPHICS_FLAG_DO_NOT_COPY_PIXELS)){
        graphics_paste_pixels_to_pixels(source_graphics, new_graphics,
                                        x, y, width, height, 0, 0,
                                        GRAPHICS_FLAG_DO_NOT_OVERWRITE_TRASPARENT_PIXELS);
    }

    // Children do NOT own their framebuffer pointer; clear the
    // clone flag so graphics_info_free does not double-kfree.
    new_graphics->flags &= ~GRAPHICS_FLAG_CLONED_FRAMEBUFFER;

    vector_push(new_graphics->parent->children, &new_graphics);
    size_t total_children = vector_count(new_graphics->parent->children);
    graphics_set_z_index(new_graphics, total_children);

out:
    if(res < 0){
        if(new_graphics){
            if(new_graphics->children){
                vector_free(new_graphics->children);
                new_graphics->children = NULL;
            }
            kfree(new_graphics);
            new_graphics = NULL;
        }
    }
    return new_graphics;
}

// Lecture 116 - tear down a surface and detach from parent.
// The root surface (loaded_graphics_info) is never freed.
void graphics_info_free(struct graphics_info* graphics_in){
    if(!graphics_in){
        return;
    }
    if(graphics_in->children){
        graphics_info_children_free(graphics_in);
    }
    if(graphics_in->pixels){
        kfree(graphics_in->pixels);
        graphics_in->pixels = NULL;
    }
    if(graphics_in->framebuffer && (graphics_in->flags & GRAPHICS_FLAG_CLONED_FRAMEBUFFER)){
        kfree(graphics_in->framebuffer);
        graphics_in->framebuffer = NULL;
    }
    if(graphics_in->parent){
        vector_pop_element(graphics_in->parent->children,
                           &graphics_in, sizeof(struct graphics_info*));
    }
    if(graphics_in == loaded_graphics_info){
        return;
    }
    kfree(graphics_in);
}

void graphics_info_children_free(struct graphics_info* graphics_info){
    if(!graphics_info){
        return;
    }
    size_t total_children = vector_count(graphics_info->children);
    for(size_t i = 0; i < total_children; i++){
        struct graphics_info* child = NULL;
        int res = vector_at(graphics_info->children, i, &child, sizeof(struct graphics_info*));
        if(res < 0){
            continue;
        }
        graphics_info_free(child);
    }
}

void graphics_setup(struct graphics_info* main_graphics_info){
    if(loaded_graphics_info){
        panic("The graphic system was already loaded\n");
    }

    real_framebuffer                     = main_graphics_info->framebuffer;
    real_framebuffer_width               = main_graphics_info->horizontal_resolution;
    real_framebuffer_height              = main_graphics_info->vertical_resolution;
    real_framebuffer_pixels_per_scanline = main_graphics_info->pixels_per_scanline;

    size_t framebuffer_size = real_framebuffer_width
                            * real_framebuffer_pixels_per_scanline
                            * sizeof(struct framebuffer_pixel);
    real_framebuffer_end = (void*)((uintptr_t)real_framebuffer + framebuffer_size);

    // Back buffer the kernel will write into. Aliased onto the
    // physical framebuffer via paging_map_to so reads/writes
    // through this pointer hit the screen.
    void* new_framebuffer_memory = kzalloc(framebuffer_size);
    main_graphics_info->framebuffer = new_framebuffer_memory;
    main_graphics_info->children    = vector_new(sizeof(struct graphics_info*), 4, 0);
    main_graphics_info->pixels      = kzalloc(framebuffer_size);
    main_graphics_info->width       = main_graphics_info->horizontal_resolution;
    main_graphics_info->height      = main_graphics_info->vertical_resolution;
    main_graphics_info->relative_x  = 0;
    main_graphics_info->relative_y  = 0;
    main_graphics_info->starting_x  = 0;
    main_graphics_info->starting_y  = 0;

    paging_map_to(kernel_desc(), new_framebuffer_memory, real_framebuffer,
                  real_framebuffer_end,
                  PAGING_IS_WRITEABLE | PAGING_IS_PRESENT);

    loaded_graphics_info = main_graphics_info;

    // Clear the back buffer to black explicitly. (kzalloc already
    // does this but the upstream pattern is to use draw_pixel so
    // that ignore_color/transparency_key logic gets exercised
    // from the start.)
    for(uint32_t y = 0; y < main_graphics_info->vertical_resolution; y++){
        for(uint32_t x = 0; x < main_graphics_info->horizontal_resolution; x++){
            struct framebuffer_pixel pixel = {0, 0, 0, 0};
            graphics_draw_pixel(main_graphics_info, x, y, pixel);
        }
    }

    graphics_info_vector = vector_new(sizeof(struct graphics_info*), 4, 0);
    vector_push(graphics_info_vector, &main_graphics_info);

    // Lecture 88 part 2 - registry init now that the screen is up.
    graphics_image_formats_init();

    // Lecture 101 - clear the framebuffer to whatever the back
    // buffer says (zeroed by L87's draw_pixel loop, i.e. black).
    // Without this the screen keeps the L86 green sanity paint
    // until something else triggers a redraw.
    graphics_redraw_all();
}

// Lecture 141 - stage-two graphics init: register the click
// handler with the mouse subsystem so any click is routed
// into graphics_mouse_click_handler.
//
// Upstream-typo function name preserved verbatim
// ("grpahics_setup_stage_two" - the `r` and `a` are swapped).
void grpahics_setup_stage_two(struct graphics_info* main_graphics_info){
    (void)main_graphics_info;
    mouse_register_click_handler(NULL, graphics_mouse_click_handler);
}
