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
#define FILE_STAT_READ_ONLY 0x01
#define FILE_STAT_DIRECTORY 0x02
int fstat(int fd, struct file_stat* file_stat_out);
// Lecture 105 - userland fopen. Thin wrapper over the
// samos_fopen syscall trampoline. "r", "w" (create
// or truncate), "a" (append); a descriptor > 0 or a negative
// status.

int  fopen(const char* filename, const char* mode);
// Lecture 106 - userland fclose. Also closes opendir descriptors.
int  fclose(int fd);
// Lecture 107 - userland fread.
int  fread(void* buffer, size_t size, size_t count, long fd);
// Lecture 111 - userland fseek. whence values mirror the kernel
// FILE_SEEK_MODE enum (SEEK_SET / SEEK_CUR / SEEK_END).
int  fseek(int fd, int offset, int whence);

// Mirror of the kernel struct file_dirent (264
// bytes, src/fs/file.h).
#define FILE_DIRENT_NAME_MAX  256
#define FILE_DIRENT_DIRECTORY 0x01
#define FILE_DIRENT_READ_ONLY 0x02
#define FILE_DIRENT_HIDDEN    0x04
#define FILE_DIRENT_SYSTEM    0x08
struct file_dirent {
    char     name[FILE_DIRENT_NAME_MAX];   // UTF-8, NUL terminated
    uint32_t size;
    uint32_t flags;
};

// Whole items written, or a negative status.
int  fwrite(const void* buffer, size_t size, size_t count, int fd);
int  ftruncate(int fd, uint32_t size);
int  unlink(const char* path);     // files only (-EISDIR)
int  mkdir(const char* path);
int  rmdir(const char* path);      // empty directories only
// A descriptor > 0; readdir returns 1 per entry, 0 at the end.
// Close with closedir (or fclose).
int  opendir(const char* path);
int  readdir(int fd, struct file_dirent* entry);
int  closedir(int fd);

// Kernel status codes (src/status.h) the file calls return,
// negated.
#define SAMOS_EIO           1
#define SAMOS_EINVARG       2
#define SAMOS_ENOMEM        3
#define SAMOS_EBADPATH      4
#define SAMOS_ERDONLY       6
#define SAMOS_ENOENT       14
#define SAMOS_ENOSPC       15
#define SAMOS_EEXIST       16
#define SAMOS_ENOTDIR      17
#define SAMOS_EISDIR       18
#define SAMOS_ENOTEMPTY    19
#define SAMOS_EFAULT       20
#define SAMOS_ENAMETOOLONG 21
// A short message for a negative status.
const char* samos_strerror(int status);

#endif
