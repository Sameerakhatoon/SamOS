#ifndef FILE_H
#define FILE_H

#include "pparser.h"
#include <stdint.h>
#include <stddef.h>

typedef unsigned int FILE_SEEK_MODE;
enum {
    SEEK_SET,
    SEEK_CUR,
    SEEK_END
};

enum {
    FILE_STAT_READ_ONLY = 0b00000001,
    FILE_STAT_DIRECTORY = 0b00000010,
};
typedef unsigned int FILE_STAT_FLAGS;

// Shared with userland through the fstat syscall, so its size
// must not change.
struct file_stat {
    FILE_STAT_FLAGS flags;
    uint32_t        filesize;
};

typedef unsigned int FILE_MODE;
enum {
    FILE_MODE_READ,
    FILE_MODE_WRITE,     // "w": create or truncate; read + write
    FILE_MODE_APPEND,    // "a": create; every write goes to the end
    FILE_MODE_INVALID
};

// One directory entry, filled in by freaddir.
#define FILE_DIRENT_NAME_MAX 256
enum {
    FILE_DIRENT_DIRECTORY = 0b00000001,
    FILE_DIRENT_READ_ONLY = 0b00000010,
    FILE_DIRENT_HIDDEN    = 0b00000100,
    FILE_DIRENT_SYSTEM    = 0b00001000,
};
struct file_dirent {
    char     name[FILE_DIRENT_NAME_MAX];   // UTF-8, NUL terminated
    uint32_t size;
    uint32_t flags;
};

struct disk;

typedef void* (*FS_OPEN_FUNCTION)(struct disk* disk, struct path_part* path, FILE_MODE mode);
typedef int   (*FS_RESOLVE_FUNCTION)(struct disk* disk);
// read/write return the number of whole items moved (C stdio
// semantics); the position advances by the bytes moved.
typedef int   (*FS_READ_FUNCTION)(struct disk* disk, void* private, uint32_t size, uint32_t nmemb, char* out);
typedef int   (*FS_WRITE_FUNCTION)(struct disk* disk, void* private, uint32_t size, uint32_t nmemb, const char* in);
typedef int   (*FS_SEEK_FUNCTION)(void* private, int64_t offset, FILE_SEEK_MODE seek_mode);
typedef int   (*FS_STAT_FUNCTION)(struct disk* disk, void* private, struct file_stat* stat);
typedef int   (*FS_CLOSE_FUNCTION)(void* private);
typedef int   (*FS_TRUNCATE_FUNCTION)(void* private, uint32_t size);
typedef int   (*FS_UNLINK_FUNCTION)(struct disk* disk, struct path_part* path);
typedef int   (*FS_MKDIR_FUNCTION)(struct disk* disk, struct path_part* path);
// path == NULL opens the root directory.
typedef void* (*FS_OPENDIR_FUNCTION)(struct disk* disk, struct path_part* path);
// Returns 1 with *entry filled, 0 at the end, or a negative status.
typedef int   (*FS_READDIR_FUNCTION)(void* private, struct file_dirent* entry);
// Lecture 84 - filesystems can report their volume label.
typedef int   (*FS_VOLUME_NAME_FUNCTION)(void* private, char* name_out, size_t max);

struct filesystem {
    FS_RESOLVE_FUNCTION     resolve;
    FS_OPEN_FUNCTION        open;
    FS_READ_FUNCTION        read;
    FS_SEEK_FUNCTION        seek;
    FS_STAT_FUNCTION        stat;
    FS_CLOSE_FUNCTION       close;
    FS_VOLUME_NAME_FUNCTION volume_name; // L84
    // Write support. Any of these may be NULL for a
    // read-only filesystem.
    FS_WRITE_FUNCTION       write;
    FS_TRUNCATE_FUNCTION    truncate;
    FS_UNLINK_FUNCTION      unlink;
    FS_MKDIR_FUNCTION       mkdir;
    FS_OPENDIR_FUNCTION     opendir;
    FS_READDIR_FUNCTION     readdir;
    char                    name[20];
};

struct file_descriptor {
    int                 index;
    struct filesystem*  filesystem;
    void*               private;
    struct disk*        disk;
    int                 is_directory;   // Opened with fopendir
};

void                fs_init();
// Returns a descriptor > 0, or 0 on any failure (unchanged API).
int                 fopen(const char* filename, const char* mode_str);
int                 fread(void* ptr, uint32_t size, uint32_t nmemb, int fd);
int                 fseek(int fd, int offset, FILE_SEEK_MODE whence);
int                 fstat(int fd, struct file_stat* stat);
int                 fclose(int fd);

// Write side of the VFS. All return a negative status on
// error.
int                 fwrite(const void* ptr, uint32_t size, uint32_t nmemb, int fd);
int                 ftruncate(int fd, uint32_t size);
int                 fremove(const char* path);      // file or empty directory
int                 fmkdir(const char* path);
// Returns a descriptor > 0 or a negative status. Close with fclose.
int                 fopendir(const char* path);
int                 freaddir(int fd, struct file_dirent* entry);

void                fs_insert_filesystem(struct filesystem* filesystem);
struct filesystem*  fs_resolve(struct disk* disk);

#endif
