// copy_from_user and friends. See uaccess.h.

#include <stdbool.h>
#include <stdint.h>
#include "arch/arch.h"
#include "arch/riscv64/uaccess.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/riscv64.h"
#include "task/process.h"
#include "status.h"

static bool user_range(const void* p, size_t n)
{
    uint64_t a = (uint64_t)p;
    return a < user_end && n <= user_end - a;
}

// Satp on `process`'s address space for one copy; returns the
// descriptor to put back (NULL: nothing to do).
static struct paging_desc* enter(struct process* process)
{
    struct paging_desc* now = paging_current_descriptor();
    if (!process || !process->paging_desc || process->paging_desc == now) {
        return NULL;
    }
    paging_switch(process->paging_desc);
    return now;
}

static void leave(struct paging_desc* prev)
{
    if (prev) {
        paging_switch(prev);
    }
}

static size_t copy(struct process* process, void* dst, const void* src, size_t n)
{
    struct paging_desc* prev = enter(process);
    csr_set(sstatus, SSTATUS_SUM);
    size_t left = uaccess_copy_bytes(dst, src, n);
    csr_clear(sstatus, SSTATUS_SUM);
    leave(prev);
    return left;
}

int copy_from_user(struct process* process, void* kdst, const void* usrc, size_t n)
{
    if (!user_range(usrc, n)) {
        return -EFAULT;
    }
    return copy(process, kdst, usrc, n) ? -EFAULT : 0;
}

int copy_to_user(struct process* process, void* udst, const void* ksrc, size_t n)
{
    if (!user_range(udst, n)) {
        return -EFAULT;
    }
    return copy(process, udst, ksrc, n) ? -EFAULT : 0;
}

long strncpy_from_user(struct process* process, char* kdst, const void* usrc, size_t max)
{
    if (max == 0) {
        return -ENAMETOOLONG;
    }
    if (!user_range(usrc, 1)) {
        kdst[0] = 0;
        return -EFAULT;
    }
    // Never read past the user half: running into it is a fault.
    size_t room = user_end - (uint64_t)usrc;
    size_t n = max < room ? max : room;
    struct paging_desc* prev = enter(process);
    csr_set(sstatus, SSTATUS_SUM);
    long len = uaccess_strncpy_bytes(kdst, usrc, n);
    csr_clear(sstatus, SSTATUS_SUM);
    leave(prev);
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

// A check, not an access: each page through the table walk,
// as on x86.
bool user_range_ok(struct process* process, const void* uptr, size_t n, bool write)
{
    if (n == 0) {
        return true;
    }
    if (!process || !user_range(uptr, n)) {
        return false;
    }
    uint64_t end = (uint64_t)uptr + n;
    for (uint64_t a = (uint64_t)uptr & ~(PAGE_SIZE - 1); a < end; a += PAGE_SIZE) {
        uint64_t pa;
        if (paging_user_translate(process->paging_desc, a, write, &pa) < 0) {
            return false;
        }
    }
    return true;
}
