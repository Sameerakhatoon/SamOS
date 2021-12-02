// Lecture 88 part 2 - BMP image decoder.
//
// Reads BITMAPFILEHEADER + BITMAPINFOHEADER and decodes 24-bit
// uncompressed pixel data into the kernel's RGBA `image` form.
// Negative bi_height means top-down; positive means bottom-up
// and we flip while reading.

#include "bmp.h"
#include "image.h"
#include "memory/heap/kheap.h"
#include "string/string.h"
#include "memory/memory.h"
#include "status.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct image* bmp_img_load(void* memory, size_t size){
    int                       res              = 0;
    struct image*             img              = NULL;
    struct bmp_header*        header           = NULL;
    struct bmp_image_header*  bmp_image_header = NULL;

    if(size < sizeof(struct bmp_header)){
        res = -EINFORMAT;
        goto out;
    }

    header = (struct bmp_header*)memory;
    if(memcmp(header->bf_type, BMP_SIGNATURE, sizeof(header->bf_type)) != 0){
        res = -EINFORMAT;
        goto out;
    }

    if(header->bf_offbits >= size){
        res = -EINFORMAT;
        goto out;
    }

    bmp_image_header = (struct bmp_image_header*)((uintptr_t)header
                                                  + sizeof(struct bmp_header));

    img = kzalloc(sizeof(struct image));
    if(!img){
        res = -ENOMEM;
        goto out;
    }

    if(bmp_image_header->bi_compression != BMP_COMPRESSION_UNCOMPRESSED){
        res = -EUNIMP;
        goto out;
    }

    uint16_t bits_per_pixel = bmp_image_header->bi_bits_per_pixel;
    if(bits_per_pixel != 24){
        res = -EUNIMP;
        goto out;
    }

    uint16_t bytes_per_pixel = bits_per_pixel / 8;

    bool    bottom_up = (bmp_image_header->bi_height > 0);
    int32_t height    = bottom_up
                        ?  bmp_image_header->bi_height
                        : -(bmp_image_header->bi_height);
    int32_t width     = bmp_image_header->bi_width;
    if(width <= 0 || height <= 0){
        res = -EINFORMAT;
        goto out;
    }

    img->width  = width;
    img->height = height;

    size_t pixel_data_size = (size_t)width * (size_t)height * sizeof(image_pixel_data);
    img->data = kzalloc(pixel_data_size);
    if(!img->data){
        res = -ENOMEM;
        goto out;
    }

    // Rows are padded up to a 4-byte boundary in the file.
    size_t raw_row_size    = (size_t)width * bytes_per_pixel;
    size_t padded_row_size = (raw_row_size + 3) & ~3u;

    if((header->bf_offbits + padded_row_size * (size_t)height) > size){
        res = -EINFORMAT;
        goto out;
    }

    uint8_t* bmp_first_pixel_ptr = (uint8_t*)memory + header->bf_offbits;
    for(int row = 0; row < height; row++){
        uint8_t* row_ptr = bmp_first_pixel_ptr + (row * padded_row_size);
        int      dest_row = bottom_up ? (height - 1 - row) : row;

        for(int col = 0; col < width; col++){
            uint8_t* bmp_pixel = row_ptr + (col * 3);
            uint8_t  B = bmp_pixel[0];
            uint8_t  G = bmp_pixel[1];
            uint8_t  R = bmp_pixel[2];

            size_t pixel_index = (size_t)dest_row * (size_t)width + (size_t)col;
            image_pixel_data* out_pix = &((image_pixel_data*)img->data)[pixel_index];
            out_pix->R = R;
            out_pix->G = G;
            out_pix->B = B;
            out_pix->A = 0;
        }
    }

out:
    if(res < 0){
        if(img){
            if(img->data){
                kfree(img->data);
            }
            kfree(img);
            img = NULL;
        }
    }
    return img;
}

void bmp_img_free(struct image* image){
    if(!image){
        return;
    }
    if(image->data){
        kfree(image->data);
    }
    kfree(image);
}

int bmp_img_format_register(struct image_format* format){
    (void)format;
    return 0;
}

void bmp_img_format_unregister(struct image_format* format){
    (void)format;
}

static struct image_format bmp_img_format = {
    .mime_type              = "image/bmp",
    .image_load_function    = bmp_img_load,
    .image_free_function    = bmp_img_free,
    .on_register_function   = bmp_img_format_register,
    .on_unregister_function = bmp_img_format_unregister,
};

struct image_format* graphics_image_format_bmp_setup(void){
    return &bmp_img_format;
}
