#ifndef SAMOS_RISCV64_UACCESS_H
#define SAMOS_RISCV64_UACCESS_H

// Checked access to user memory, x86's contract (
// src/task/uaccess.h) with return values from status.h, on the
// address space satp holds now (no struct process until the riscv64 user-mode work).
//
// x86 walks the process's tables and copies through the direct map.
// Here the hardware checks instead: the copy runs on the user
// address itself with sstatus.SUM set for its duration only, and a
// fault (unmapped, read-only, no U bit) comes back through the
// exception fixup (uaccess.S, trap.c) as -EFAULT. A range that is not
// wholly below user_end (a kernel pointer, or one that wraps) is
// refused before any access.

#include <stddef.h>

// 0 or -EFAULT. On -EFAULT the bytes before the fault are copied.
int  copy_from_user(void* kdst, const void* usrc, size_t n);
int  copy_to_user(void* udst, const void* ksrc, size_t n);
// Copy a NUL-terminated user string into kdst[max]. kdst is always
// NUL-terminated. Returns the length, -EFAULT for a bad pointer or
// -ENAMETOOLONG if no NUL fits (kdst holds the first max-1 bytes).
long strncpy_from_user(char* kdst, const void* usrc, size_t max);

// uaccess.S, called with SUM set.
size_t uaccess_copy_bytes(void* dst, const void* src, size_t n);
long   uaccess_strncpy_bytes(char* dst, const char* src, size_t max);

#endif
