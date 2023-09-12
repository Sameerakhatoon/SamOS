#ifndef USERLAND_FILE_H
#define USERLAND_FILE_H

#include <stddef.h>
#include <stdint.h>   // L110 - explicit, mirrors upstream

// Lecture 112 - userland mirror of the kernel struct file_stat.
// Layout must match src/fs/file.h (FILE_STAT_FLAGS + filesize).
typedef unsigned int FILE_STAT_FLAGS;
struct file_stat {
    FILE_STAT_FLAGS flags;
    uint32_t        filesize;
};
int fstat(int fd, struct file_stat* file_stat_out);
// Lecture 105 - userland fopen. Thin wrapper over the
// samos_fopen syscall trampoline.

int  fopen(const char* filename, const char* mode);
// Lecture 106 - userland fclose.
void fclose(int fd);
// Lecture 107 - userland fread.
int  fread(void* buffer, size_t size, size_t count, long fd);
// Lecture 111 - userland fseek. whence values mirror the kernel
// FILE_SEEK_MODE enum (SEEK_SET / SEEK_CUR / SEEK_END).
int  fseek(int fd, int offset, int whence);

#endif
