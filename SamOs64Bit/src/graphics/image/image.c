// Lecture 88 (part 1) - image abstraction implementation.
//
// Holds a vector of image_format* and lets the rest of the
// kernel load an image by path (via fopen/fread) without
// knowing which decoder ran. L88 part 2 registers BMP.
//
// Notable upstream patterns we keep verbatim:
//   - graphics_image_load is missing a `return img` in upstream;
//     we add one because nothing else compiles without it. (The
//     upstream binary works only because optimisation reuses the
//     rax that already holds `img`. We do not rely on that.)
//   - The "grpahics_image_format_unload" typo is preserved as
//     an alias; the canonical spelling forwards to it.

#include "graphics/image/image.h"
#include "graphics/image/bmp.h"
#include "graphics/graphics.h"
#include "memory/memory.h"
#include "fs/file.h"
#include "lib/vector/vector.h"
#include "status.h"
#include "string/string.h"
#include "memory/heap/kheap.h"

static struct vector* image_formats = NULL;

struct image_format* graphics_image_format_get(const char* mime_type){
    struct image_format* format = NULL;
    for(size_t i = 0; i < vector_count(image_formats); i++){
        struct image_format* current_format = NULL;
        int res = vector_at(image_formats, i, &current_format, sizeof(struct image_format*));
        if(res < 0){
            break;
        }
        if(current_format
           && strncmp(current_format->mime_type, mime_type,
                      sizeof(current_format->mime_type)) == 0){
            format = current_format;
            break;
        }
    }
    return format;
}

struct image* graphics_image_load_from_memory(void* memory, size_t max){
    struct image* image_out = NULL;
    size_t total_formats = vector_count(image_formats);
    for(size_t i = 0; i < total_formats; i++){
        struct image_format* fmt = NULL;
        int res = vector_at(image_formats, i, &fmt, sizeof(fmt));
        if(res < 0){
            break;
        }
        image_out = fmt->image_load_function(memory, max);
        if(image_out){
            image_out->format = fmt;
            break;
        }
    }
    return image_out;
}

image_pixel_data graphics_image_get_pixel(struct image* image, int x, int y){
    image_pixel_data pixel_data = image->data[y * image->width + x];
    return pixel_data;
}

struct image* graphics_image_load(const char* path){
    struct image* img        = NULL;
    void*         img_memory = NULL;
    int           fd         = 0;
    int           res        = 0;

    fd = fopen(path, "r");
    if(fd < 0){
        res = fd;
        goto out;
    }

    struct file_stat stat;
    res = fstat(fd, &stat);
    if(res < 0){
        goto out;
    }

    img_memory = kzalloc(stat.filesize);
    if(!img_memory){
        res = -ENOMEM;
        goto out;
    }

    res = fread(img_memory, stat.filesize, 1, fd);
    if(res < 0){
        goto out;
    }

    img = graphics_image_load_from_memory(img_memory, stat.filesize);
    if(!img){
        res = -EINFORMAT;
        goto out;
    }

out:
    if(fd > 0){
        fclose(fd);
    }
    if(img_memory){
        kfree(img_memory);
        img_memory = NULL;
    }
    if(res < 0){
        if(img){
            graphics_image_free(img);
            img = NULL;
        }
    }
    // L88 fix: upstream forgot this return. Without it the C
    // standard says the result of the call is undefined.
    return img;
}

void graphics_image_free(struct image* image){
    image->format->image_free_function(image);
}

// Upstream-spelling typo, kept verbatim for diff hygiene.
void grpahics_image_format_unload(struct image_format* format){
    if(format->on_unregister_function){
        format->on_unregister_function(format);
    }
}

// Canonical-spelling alias.
void graphics_image_format_unload(struct image_format* format){
    grpahics_image_format_unload(format);
}

int graphics_image_format_register(struct image_format* format){
    int res = 0;
    struct image_format* existing_format = graphics_image_format_get(format->mime_type);
    if(existing_format){
        res = -EISTKN;
        goto out;
    }

    res = vector_push(image_formats, &format);
    if(res < 0){
        goto out;
    }

    res = format->on_register_function(format);
    if(res < 0){
        goto out;
    }

out:
    if(res < 0){
        if(format){
            grpahics_image_format_unload(format);
        }
    }
    return res;
}

int graphics_image_formats_load(void){
    // L88 part 2 - BMP is the only built-in decoder.
    graphics_image_format_register(graphics_image_format_bmp_setup());
    return 0;
}

int graphics_image_formats_init(void){
    int res = 0;
    image_formats = vector_new(sizeof(struct image_format*), 10, VECTOR_NO_FLAGS);
    if(!image_formats){
        res = -ENOMEM;
        goto out;
    }

    res = graphics_image_formats_load();
    if(res < 0){
        goto out;
    }

out:
    if(res < 0){
        if(image_formats){
            vector_free(image_formats);
            image_formats = NULL;
        }
    }
    return res;
}

void graphics_image_formats_unload(void){
    while(vector_count(image_formats) > 0){
        struct image_format* format = NULL;
        vector_back(image_formats, &format, sizeof(struct image_format*));
        if(format){
            grpahics_image_format_unload(format);
        }
        vector_pop(image_formats);
    }
}
