#include "stdlib.h"
#include "samos.h"

void* malloc(size_t size){
    return samos_malloc(size);
}

void free(void* ptr){
    (void)ptr;
}
