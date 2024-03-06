// task/uaccess.h on aarch64 (x86's contract, the shared
// task/uaccess.c's return values). Instead of walking the tables in
// software, the copy goes through the process's own TTBR0 with
// unprivileged loads and stores (uaccess.S): the MMU applies EL0's
// permissions, a fault is caught through __ex_table, and with PAN the
// kernel's ordinary loads cannot reach user memory at all.
//
// A syscall runs on the kernel descriptor (kernel_page: TTBR0 empty),
// and a copy may be for another process than the current one, so each
// call switches TTBR0 to the process's space for the copy and back.
// Nothing in between switches it: interrupts taken here are kernel-mode
// traps, which never change address space (trap.c).

#include "task/uaccess.h"
#include "task/process.h"
#include "arch/arch.h"
#include "config.h"
#include "status.h"

size_t uaccess_copy_from(void* kdst, const void* usrc, size_t n);
size_t uaccess_copy_to(void* udst, const void* ksrc, size_t n);
long   uaccess_strncpy(char* kdst, const void* usrc, size_t max);
uint64_t paging_desc_ttbr0(struct paging_desc* desc);

static bool range_valid(uintptr_t uva, size_t n)
{
    return uva < SAMOS_USER_SPACE_END && n <= SAMOS_USER_SPACE_END - uva;
}

// TTBR0 of the process in, the old value out; 0 without a space.
static uint64_t space_enter(struct process* process)
{
    if (!process || !process->paging_desc)
        return 0;
    uint64_t old, want = paging_desc_ttbr0(process->paging_desc);
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(old));
    if (old != want)
        __asm__ volatile("msr ttbr0_el1, %0\n\tisb" :: "r"(want) : "memory");
    return old;
}

static void space_leave(uint64_t old)
{
    __asm__ volatile("msr ttbr0_el1, %0\n\tisb" :: "r"(old) : "memory");
}

int copy_from_user(struct process* process, void* kdst, const void* usrc, size_t n)
{
    if (n == 0)
        return 0;
    if (!range_valid((uintptr_t)usrc, n))
        return -EFAULT;
    uint64_t old = space_enter(process);
    if (!old)
        return -EFAULT;
    size_t left = uaccess_copy_from(kdst, usrc, n);
    space_leave(old);
    return left ? -EFAULT : 0;
}

int copy_to_user(struct process* process, void* udst, const void* ksrc, size_t n)
{
    if (n == 0)
        return 0;
    if (!range_valid((uintptr_t)udst, n))
        return -EFAULT;
    uint64_t old = space_enter(process);
    if (!old)
        return -EFAULT;
    size_t left = uaccess_copy_to(udst, ksrc, n);
    space_leave(old);
    return left ? -EFAULT : 0;
}

// Asks the tables, touches nothing (as the shared one).
bool user_range_ok(struct process* process, const void* uptr, size_t n, bool write)
{
    uintptr_t uva = (uintptr_t)uptr;
    uint64_t phys;
    if (n == 0)
        return true;
    if (!process || !range_valid(uva, n))
        return false;
    for (uintptr_t p = uva & ~(uintptr_t)(PAGING_PAGE_SIZE - 1); p < uva + n; p += PAGING_PAGE_SIZE)
        if (paging_user_translate(process->paging_desc, p, write, &phys) < 0)
            return false;
    return true;
}

long strncpy_from_user(struct process* process, char* kdst, const void* usrc, size_t max)
{
    uintptr_t uva = (uintptr_t)usrc;
    if (max == 0)
        return -ENAMETOOLONG;
    if (!range_valid(uva, 1)) {
        kdst[0] = 0;
        return -EFAULT;
    }
    // Never read past the user half: running into it is a fault.
    size_t room = SAMOS_USER_SPACE_END - uva;
    size_t n = max < room ? max : room;
    uint64_t old = space_enter(process);
    if (!old) {
        kdst[0] = 0;
        return -EFAULT;
    }
    long len = uaccess_strncpy(kdst, usrc, n);
    space_leave(old);
    if (len < 0 || ((size_t)len == n && n < max)) {
        kdst[0] = 0;
        return -EFAULT;
    }
    if ((size_t)len == max) {
        kdst[max - 1] = 0;
        return -ENAMETOOLONG;
    }
    return len;
}
