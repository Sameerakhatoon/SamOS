#ifndef GRAPHICS_IMAGE_FORMAT_BMP_H
#define GRAPHICS_IMAGE_FORMAT_BMP_H
// Lecture 88 part 2 - BMP decoder. Speaks the classic two-header
// Windows v3 form (BITMAPFILEHEADER + BITMAPINFOHEADER), 24-bit
// uncompressed only. Bigger BMPs (4/8/16 bpp, RLE, BITFIELDS)
// would need a palette and a different per-pixel walk; we stop
// at -EUNIMP and let the registry try the next format.

#include "graphics/image/image.h"
#include <stddef.h>
#include <stdint.h>

#define BIT_PER_PIXEL_MONOCHROME       1
#define BIT_PER_PIXEL_16_COLORS        4
#define BIT_PER_PIXEL_256_COLORS       8
#define BIT_PER_PIXEL_65536_COLORS     16
#define BIT_PER_PIXEL_16777216_COLORS  24

#define BMP_SIGNATURE                  "BM"

#define BMP_COMPRESSION_UNCOMPRESSED   0
#define BMP_COMPRESSION_RLE8           1
#define BMP_COMPRESSION_RLE4           2
#define BMP_COMPRESSION_BITFIELDS      3

struct bmp_header {
    char     bf_type[2];     // "BM"
    uint32_t bf_size;        // file size in bytes
    uint16_t bf_reserved_1;
    uint16_t bf_reserved_2;
    uint32_t bf_offbits;     // byte offset of pixel data from start of file
} __attribute__((packed));

struct bmp_image_header {
    uint32_t bi_size;
    int32_t  bi_width;
    int32_t  bi_height;
    uint16_t bi_planes;
    uint16_t bi_bits_per_pixel;
    uint32_t bi_compression;
    uint32_t bi_size_image;
    uint32_t bi_x_pixels_per_m;
    uint32_t bi_y_pixels_per_m;
    uint32_t bi_colors_used_count;
    uint32_t bi_important_colors_count;
} __attribute__((packed));

// Palette entry for the bit-depths we do not handle (kept for
// shape compat; never read).
struct color_table {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t reserved;
};

struct image_format* graphics_image_format_bmp_setup(void);

#endif
