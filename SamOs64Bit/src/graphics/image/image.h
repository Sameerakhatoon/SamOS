#ifndef GRAPHICS_IMAGE_H
#define GRAPHICS_IMAGE_H
// Lecture 88 (part 1) - image abstraction. A registry of image
// formats; each registered format reports a MIME string plus a
// load/free callback. The kernel only knows about `struct image`
// and `image_pixel_data` here; concrete decoders (BMP in L88
// part 2) plug in via graphics_image_format_register.

#include <stdint.h>
#include <stddef.h>

struct image_format;

typedef union image_pixel_data {
    uint32_t data;     // Full 32-bit value (RGBA, host-endian).
    struct {
        uint8_t R;
        uint8_t G;
        uint8_t B;
        uint8_t A;
    };
} image_pixel_data;

struct image {
    uint32_t          width;
    uint32_t          height;
    image_pixel_data* data;     // row-major, width*height entries

    void*             private;  // format-private (e.g. BMP header copy)
    struct image_format* format;
};

typedef struct image* (*image_load_function)(void* memory, size_t size);
typedef void          (*image_free_function)(struct image* image);
typedef int           (*image_format_register_function)(struct image_format* format);
typedef void          (*image_format_unregister_function)(struct image_format* format);

#define IMAGE_FORMAT_MAX_MIME_TYPE 64

struct image_format {
    char                              mime_type[IMAGE_FORMAT_MAX_MIME_TYPE];
    image_load_function               image_load_function;
    image_free_function               image_free_function;
    image_format_register_function    on_register_function;
    image_format_unregister_function  on_unregister_function;
    void*                             private;
};

// Public API. The init function builds the registry; the load
// path walks every registered format and uses the first that
// recognises the bytes.
int                  graphics_image_formats_init(void);
int                  graphics_image_formats_load(void);   // hook for L88p2
void                 graphics_image_formats_unload(void);

int                  graphics_image_format_register(struct image_format* format);
struct image_format* graphics_image_format_get(const char* mime_type);

struct image*        graphics_image_load(const char* path);
struct image*        graphics_image_load_from_memory(void* memory, size_t max);
void                 graphics_image_free(struct image* image);
image_pixel_data     graphics_image_get_pixel(struct image* image, int x, int y);

// Note the upstream typo `grpahics_image_format_unload`; preserved
// to keep our diff against upstream readable. We expose an alias
// under the correct spelling so callers do not propagate the typo.
void                 grpahics_image_format_unload(struct image_format* format);
void                 graphics_image_format_unload(struct image_format* format);

#endif
