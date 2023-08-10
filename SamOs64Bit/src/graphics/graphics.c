// Lecture 87 - graphics foundation. Implements
// graphics_setup/draw/redraw on top of the UEFI-supplied
// framebuffer. The root graphics_info (default_graphics_info)
// lives in kernel.asm so the long-mode entry can stash the
// UEFI register payload directly into it.

#include "graphics.h"
#include "graphics/image/image.h"
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

struct graphics_info* graphics_screen_info(void){
    return loaded_graphics_info;
}

// Copy a (clipped) rectangle from `src_info->pixels` to the
// physical framebuffer at (dst_abs_x, dst_abs_y). Skips pixels
// matching src_info->transparency_key. Black-key means "no key".
static void graphics_paste_pixels_to_framebuffer(struct graphics_info* src_info,
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

static void graphics_redraw_only(struct graphics_info* g){
    if(!g){
        return;
    }
    graphics_paste_pixels_to_framebuffer(g, 0, 0, g->width, g->height,
                                         g->starting_x, g->starting_y);
}

void graphics_redraw(struct graphics_info* g){
    if(!g){
        return;
    }
    graphics_redraw_only(g);
    // TODO: walk children once the compositor lands.
}

void graphics_redraw_all(void){
    graphics_redraw(graphics_screen_info());
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
}
