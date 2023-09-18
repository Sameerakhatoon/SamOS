#include "stdlib.h"
#include "samos.h"
#include "memory.h"   // L115 - memset for calloc

void* malloc(size_t size){
    return samos_malloc(size);
}

void free(void* ptr){
    samos_free(ptr);
}

// Lecture 115 - userland calloc. malloc + memset(0).
void* calloc(size_t n_memb, size_t size){
    size_t b_size = n_memb * size;
    void*  ptr    = malloc(b_size);
    if(!ptr){
        return NULL;
    }
    memset(ptr, 0, b_size);
    return ptr;
}

void* realloc(void* ptr, size_t new_size){
    return samos_realloc(ptr, new_size);
}

char* itoa(int i){
    static char text[12];
    int loc = 11;
    text[11] = 0;
    char neg = 1;
    if(i >= 0){
        neg = 0;
        i = -i;
    }
    while(i){
        text[--loc] = '0' - (i % 10);
        i /= 10;
    }
    if(loc == 11){
        text[--loc] = '0';
    }
    if(neg){
        text[--loc] = '-';
    }
    return &text[loc];
}
