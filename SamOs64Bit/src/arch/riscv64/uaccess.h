#ifndef SAMOS_RISCV64_UACCESS_H
#define SAMOS_RISCV64_UACCESS_H

// Checked access to user memory, x86's contract (
// src/task/uaccess.h) with return values from status.h.
//
// x86 walks the process's tables and copies through the direct map.
// Here the hardware checks instead: the copy runs on the user
// address itself with sstatus.SUM set for its duration only, and a
// fault (unmapped, read-only, no U bit) comes back through the
// exception fixup (uaccess.S, trap.c) as -EFAULT. A range that is not
// wholly below user_end (a kernel pointer, or one that wraps) is
// refused before any access.
//
// This file implements task/uaccess.h itself (the Makefile
// leaves the shared task/uaccess.o out on riscv64). The copy runs on
// `process`'s address space: if satp holds another one (the trap path
// has switched to the kernel root, kernel_page), it is loaded for the
// copy and put back, which with ASIDs costs no TLB flush. A NULL
// process means the address space satp holds now (the riscv64 MMU code's tests).

#include <stddef.h>
#include "task/uaccess.h"

// uaccess.S, called with SUM set.
size_t uaccess_copy_bytes(void* dst, const void* src, size_t n);
long   uaccess_strncpy_bytes(char* dst, const char* src, size_t max);

#endif
