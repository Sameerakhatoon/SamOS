// Lecture 105 - userland fopen wrapper.

#include "file.h"
#include "samos.h"
#include <stdint.h>
#include <stddef.h>

int fopen(const char* filename, const char* mode){
    return (int)samos_fopen(filename, mode);
}

int fclose(int fd){
    return (int)samos_fclose((size_t)fd);
}

int fread(void* buffer, size_t size, size_t count, long fd){
    return samos_fread(buffer, size, count, fd);
}

int fseek(int fd, int offset, int whence){
    return (int)samos_fseek(fd, offset, whence);
}

int fstat(int fd, struct file_stat* file_stat_out){
    return (int)samos_fstat(fd, file_stat_out);
}

int fwrite(const void* buffer, size_t size, size_t count, int fd){
    return (int)samos_fwrite(buffer, size, count, fd);
}

int ftruncate(int fd, uint32_t size){
    return samos_ftruncate(fd, size);
}

int unlink(const char* path){
    return samos_unlink(path);
}

int mkdir(const char* path){
    return samos_mkdir(path);
}

int rmdir(const char* path){
    return samos_rmdir(path);
}

int opendir(const char* path){
    return samos_opendir(path);
}

int readdir(int fd, struct file_dirent* entry){
    return samos_readdir(fd, entry);
}

int closedir(int fd){
    return fclose(fd);
}

const char* samos_strerror(int status){
    switch(-status){
        case SAMOS_EBADPATH:     return "bad path";
        case SAMOS_ERDONLY:      return "read-only";
        case SAMOS_ENOMEM:       return "out of memory";
        case SAMOS_ENOENT:       return "no such file or directory";
        case SAMOS_ENOSPC:       return "no space left";
        case SAMOS_EEXIST:       return "already exists";
        case SAMOS_ENOTDIR:      return "not a directory";
        case SAMOS_EISDIR:       return "is a directory";
        case SAMOS_ENOTEMPTY:    return "directory not empty";
        case SAMOS_EFAULT:       return "bad address";
        case SAMOS_ENAMETOOLONG: return "name too long";
        case SAMOS_EINVARG:      return "invalid argument";
        default:                 return "I/O error";
    }
}
