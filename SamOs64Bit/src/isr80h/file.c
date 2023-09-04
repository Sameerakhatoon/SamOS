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
#include <stddef.h>
#include <stdint.h>

// Lecture 106 - userland fclose. Reads the fd off the syscall
// stack and forwards to process_fclose, which finds the matching
// process_file_handle, calls the kernel fclose, and pops the
// record off the per-process vector.
void* isr80h_command11_fclose(struct interrupt_frame* frame){
    (void)frame;
    int64_t fd = (int64_t)task_get_stack_item(task_current(), 0);
    process_fclose(task_current()->process, fd);
    return NULL;
}

void* isr80h_command10_fopen(struct interrupt_frame* frame){
    (void)frame;
    int   fd                  = 0;
    void* filename_virt_addr  = NULL;
    void* mode_virt_addr      = NULL;

    filename_virt_addr = task_get_stack_item(task_current(), 0);
    filename_virt_addr = task_virtual_address_to_physical(task_current(), filename_virt_addr);
    if(!filename_virt_addr){
        fd = -1;
        goto out;
    }

    mode_virt_addr = task_get_stack_item(task_current(), 1);
    mode_virt_addr = task_virtual_address_to_physical(task_current(), mode_virt_addr);
    if(!mode_virt_addr){
        fd = -1;
        goto out;
    }

    fd = process_fopen(task_current()->process, filename_virt_addr, mode_virt_addr);
    if(fd <= 0){
        goto out;
    }

out:
    return (void*)(int64_t)fd;
}
