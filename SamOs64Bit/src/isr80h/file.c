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
    (void)frame;
    int64_t fd = (int64_t)isr80h_arg(frame, 0);
    process_fclose(task_current()->process, fd);
    return NULL;
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
