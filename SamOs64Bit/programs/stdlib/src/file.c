// Lecture 105 - userland fopen wrapper.

#include "file.h"
#include "samos.h"
#include <stdint.h>
#include <stddef.h>

int fopen(const char* filename, const char* mode){
    return (int)samos_fopen(filename, mode);
}

void fclose(int fd){
    samos_fclose((size_t)fd);
}

int fread(void* buffer, size_t size, size_t count, long fd){
    return samos_read(buffer, size, count, fd);
}
