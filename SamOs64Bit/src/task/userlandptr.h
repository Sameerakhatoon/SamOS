#ifndef KERNEL_USERLAND_PTR_H
#define KERNEL_USERLAND_PTR_H
// Lecture 153 - userland pointers.
//
// A `struct userland_ptr` is a sentinel value the kernel hands
// back to userland in place of a real kernel pointer. The
// kernel maps the sentinel to its actual kernel_ptr through
// the per-process registry. Userland can pass the sentinel
// back to syscalls; the kernel re-resolves it without exposing
// any real kernel addresses.

#include <stdbool.h>
#include <stdint.h>

struct userland_ptr {
    void* kernel_ptr;
};

struct process;

struct userland_ptr* process_userland_pointer_create(struct process* process, void* kernel_ptr);
void                 process_userland_pointer_release(struct process* process, void* userland_ptr);
bool                 process_userland_pointer_registered(struct process* process, void* userland_ptr);
void*                process_userland_pointer_kernel_ptr(struct process* process, void* userland_ptr);

#endif
