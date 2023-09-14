#ifndef SAMOS_STDLIB_H
#define SAMOS_STDLIB_H

#include <stddef.h>

#define LONG_MAX  0x7FFFFFFFFFFFFFFFL
#define LONG_MIN  (-LONG_MAX - 1L)
#define INT_MAX   0x7FFFFFFF
#define INT_MIN   (-INT_MAX - 1)

void*  malloc(size_t size);
void   free(void* ptr);

// Not reentrant (static buffer); kept for existing callers.
char*  itoa(int i);
// Reentrant: writes into buffer, base 2..36. Returns buffer
// or NULL when it does not fit.
char*  itoa_r(int value, char* buffer, size_t size, int base);

// Lecture 115 - calloc + realloc on top of the L115 realloc
// syscall. Calloc returns NULL (errno = ENOMEM) when
// n_memb * size overflows.
void*  calloc(size_t n_memb, size_t size);
void*  realloc(void* ptr, size_t new_size);

// Number parsing. strtol / strtoul accept leading space, a
// sign, and (base 0 or 16) a 0x prefix; base 0 also takes a
// leading 0 as octal. Out-of-range values saturate and set errno =
// ERANGE.
long          strtol(const char* str, char** endptr, int base);
unsigned long strtoul(const char* str, char** endptr, int base);
int           atoi(const char* str);
long          atol(const char* str);
int           abs(int value);

#endif
