#ifndef USERLAND_FILE_H
#define USERLAND_FILE_H

#include <stddef.h>
// Lecture 105 - userland fopen. Thin wrapper over the
// samos_fopen syscall trampoline.

int  fopen(const char* filename, const char* mode);
// Lecture 106 - userland fclose.
void fclose(int fd);
// Lecture 107 - userland fread.
int  fread(void* buffer, size_t size, size_t count, long fd);

#endif
