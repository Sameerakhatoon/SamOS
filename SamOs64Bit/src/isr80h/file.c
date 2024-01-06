// Lecture 105 - userland fopen handler.
//
// Userland pushes (filename_ptr, mode_ptr) onto its stack, both
// task-virtual addresses. We translate them to kernel-physical,
// call process_fopen which wraps the kernel fopen and records
// the descriptor on the process, and return the fd.

#include "file.h"
#include "task/task.h"
#include "task/process.h"
#include "idt/idt.h"
#include "fs/file.h"   // L112 - struct file_stat for fstat handler
#include "task/uaccess.h"
#include "status.h"
#include "kernel.h"
#include <stddef.h>
#include <stdint.h>

// Lecture 112 - userland fstat. Stack: 0=fd, 1=struct file_stat*.
// process_fstat translates the buffer pointer and forwards to
// the kernel fstat for the underlying fd.
void* isr80h_command14_fstat(struct trap_frame* frame){
    (void)frame;
    long fd = (long)isr80h_arg(frame, 0);
    struct file_stat* virt_file_stat_addr =
        (struct file_stat*)isr80h_arg(frame, 1);
    return (void*)(long)process_fstat(task_current()->process, fd, virt_file_stat_addr);
}

// Lecture 111 - userland fseek. Stack layout:
//   0: fd, 1: offset, 2: whence
void* isr80h_command13_fseek(struct trap_frame* frame){
    (void)frame;
    long fd     = (long)isr80h_arg(frame, 0);
    long offset = (long)isr80h_arg(frame, 1);
    long whence = (long)isr80h_arg(frame, 2);
    return (void*)(long)process_fseek(task_current()->process, fd, offset, whence);
}

// Lecture 107 - userland fread. Stack layout (top to bottom):
//   0: buffer (user virtual)
//   1: size
//   2: count
//   3: fd
// process_fread translates the buffer pointer and forwards to
// the kernel fread.
void* isr80h_command12_fread(struct trap_frame* frame){
    (void)frame;
    int    res             = 0;
    void*  buffer_virt_addr = isr80h_arg(frame, 0);
    size_t size            = (size_t)isr80h_arg(frame, 1);
    size_t count           = (size_t)isr80h_arg(frame, 2);
    long   fd              = (long)isr80h_arg(frame, 3);

    res = process_fread(task_current()->process, buffer_virt_addr, size, count, fd);
    return (void*)(int64_t)res;
}

// Lecture 106 - userland fclose. Reads the fd off the syscall
// stack and forwards to process_fclose, which finds the matching
// process_file_handle, calls the kernel fclose, and pops the
// record off the per-process vector.
void* isr80h_command11_fclose(struct trap_frame* frame){
    // Returns 0, or -EINVARG for a descriptor the
    // process does not hold. The stdlib wrapper really calls it now.
    int64_t fd = (int64_t)isr80h_arg(frame, 0);
    return ERROR(process_fclose(task_current()->process, fd));
}

// The book translated the first byte of each string
// and handed the result to fopen as a kernel pointer, so the strings
// could run on past their page. Both are copied into kernel buffers
// now; a path that does not fit SAMOS_MAX_PATH is -ENAMETOOLONG.
void* isr80h_command10_fopen(struct trap_frame* frame){
    (void)frame;
    struct process* process = task_current()->process;
    char filename[SAMOS_MAX_PATH];
    char mode[4];

    long res = strncpy_from_user(process, filename, isr80h_arg(frame, 0),
                                 sizeof(filename));
    if(res >= 0){
        res = strncpy_from_user(process, mode, isr80h_arg(frame, 1),
                                sizeof(mode));
    }
    if(res < 0){
        return ERROR(res);
    }
    return (void*)(int64_t)process_fopen(process, filename, mode);
}

// ---------------------------------------------------------------
// The write side of the VFS and directories, for the
// shell's mkdir, write, ls, rm and rmdir. Arguments in registers;
// paths are copied with strncpy_from_user and buffers go
// through uaccess page by page (process_fwrite, process_freaddir).
// ---------------------------------------------------------------

static long file_user_path(struct process* process, char* out, void* uptr){
    return strncpy_from_user(process, out, uptr, SAMOS_MAX_PATH);
}

// 1 for a directory, 0 for a file, or a negative status.
static int file_path_is_dir(const char* path){
    int fd = fopen_status(path, "r");
    if(fd < 0){
        return fd;
    }
    struct file_stat stat = {0};
    int res = fstat(fd, &stat);
    fclose(fd);
    if(res < 0){
        return res;
    }
    return (stat.flags & FILE_STAT_DIRECTORY) ? 1 : 0;
}

// rdi = buffer, rsi = size, rdx = count, r10 = fd. Whole items
// written, or a negative status.
void* isr80h_command40_fwrite(struct trap_frame* frame){
    return (void*)(int64_t)process_fwrite(task_current()->process,
                                          isr80h_arg(frame, 0),
                                          (uint64_t)isr80h_arg(frame, 1),
                                          (uint64_t)isr80h_arg(frame, 2),
                                          (int)(int64_t)isr80h_arg(frame, 3));
}

// rdi = fd, rsi = new size. The file must be open with "w" or "a".
void* isr80h_command41_ftruncate(struct trap_frame* frame){
    uint64_t size = (uint64_t)isr80h_arg(frame, 1);
    if(size > 0xFFFFFFFFull){
        return ERROR(-EINVARG);
    }
    return (void*)(int64_t)process_ftruncate(task_current()->process,
                                             (int)(int64_t)isr80h_arg(frame, 0),
                                             (uint32_t)size);
}

// unlink (dir = 0) refuses a directory with -EISDIR, rmdir (dir = 1)
// refuses a file with -ENOTDIR; the VFS fremove takes either.
// The type check and the remove in one hold of the VFS lock.
static void* file_remove(struct trap_frame* frame, int dir){
    char path[SAMOS_MAX_PATH];
    long res = file_user_path(task_current()->process, path, isr80h_arg(frame, 0));
    vfs_lock();
    if(res >= 0){
        res = file_path_is_dir(path);
    }
    if(res >= 0 && res != dir){
        res = dir ? -ENOTDIR : -EISDIR;
    }
    if(res >= 0){
        res = fremove(path);
    }
    vfs_unlock();
    return ERROR(res);
}

// rdi = path.
void* isr80h_command42_unlink(struct trap_frame* frame){
    return file_remove(frame, 0);
}

// rdi = path. -EEXIST, -ENOTDIR, or -ENOENT for a missing parent.
void* isr80h_command43_mkdir(struct trap_frame* frame){
    char path[SAMOS_MAX_PATH];
    long res = file_user_path(task_current()->process, path, isr80h_arg(frame, 0));
    if(res >= 0){
        res = fmkdir(path);
    }
    return ERROR(res);
}

// rdi = path. Only an empty directory (-ENOTEMPTY otherwise).
void* isr80h_command44_rmdir(struct trap_frame* frame){
    return file_remove(frame, 1);
}

// rdi = path ("0:/" is the root). A descriptor > 0, closed with
// fclose (and on exit), or a negative status.
void* isr80h_command45_opendir(struct trap_frame* frame){
    struct process* process = task_current()->process;
    char path[SAMOS_MAX_PATH];
    long res = file_user_path(process, path, isr80h_arg(frame, 0));
    if(res >= 0){
        res = process_fopendir(process, path);
    }
    return ERROR(res);
}

// rdi = fd, rsi = struct file_dirent* (264 bytes). 1 with an entry,
// 0 at the end, or a negative status.
void* isr80h_command46_readdir(struct trap_frame* frame){
    return (void*)(int64_t)process_freaddir(task_current()->process,
                                            (int)(int64_t)isr80h_arg(frame, 0),
                                            (struct file_dirent*)isr80h_arg(frame, 1));
}
