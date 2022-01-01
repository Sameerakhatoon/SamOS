#ifndef SAMOS_STDLIB_H
#define SAMOS_STDLIB_H

#include <stddef.h>

void*  malloc(size_t size);
void   free(void* ptr);
char*  itoa(int i);

// Lecture 115 - calloc + realloc on top of the L115 realloc
// syscall.
void*  calloc(size_t n_memb, size_t size);
void*  realloc(void* ptr, size_t new_size);

#endif
