// Checked access to user memory.
//
// The book's syscalls translated only the first byte of a user
// pointer and then used the result as a kernel pointer, so a buffer
// could run past its page, a kernel address went straight through
// and a missing NUL let strcpy run off a 108-byte stack buffer
// (a kernel stack overflow). Every syscall now goes through these. Each
// page is checked against the process's own page tables (present,
// U=1, and RW=1 for a write) and reached through the direct map,
// so it works from any address space and never touches a kernel address.

#ifndef UACCESS_H
#define UACCESS_H

#include <stddef.h>
#include <stdbool.h>

struct process;

// 0 or -EFAULT. Nothing is guaranteed copied on -EFAULT.
int  copy_from_user(struct process* process, void* kdst, const void* usrc, size_t n);
int  copy_to_user(struct process* process, void* udst, const void* ksrc, size_t n);

// Copy a NUL-terminated user string into kdst[max]. kdst is always
// NUL-terminated. Returns the length, -EFAULT for a bad pointer or
// -ENAMETOOLONG if no NUL fits (kdst holds the first max-1 bytes).
long strncpy_from_user(struct process* process, char* kdst, const void* usrc, size_t max);

// True if all of [uptr, uptr + n) is mapped for the user (writable
// when `write`).
bool user_range_ok(struct process* process, const void* uptr, size_t n, bool write);

#endif
