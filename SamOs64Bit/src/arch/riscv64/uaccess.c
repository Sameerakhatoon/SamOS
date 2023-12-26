// copy_from_user and friends. See uaccess.h.

#include <stdbool.h>
#include <stdint.h>
#include "arch/riscv64/uaccess.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/riscv64.h"
#include "status.h"

static bool user_range(const void* p, size_t n)
{
    uint64_t a = (uint64_t)p;
    return a < user_end && n <= user_end - a;
}

static size_t copy(void* dst, const void* src, size_t n)
{
    csr_set(sstatus, SSTATUS_SUM);
    size_t left = uaccess_copy_bytes(dst, src, n);
    csr_clear(sstatus, SSTATUS_SUM);
    return left;
}

int copy_from_user(void* kdst, const void* usrc, size_t n)
{
    if (!user_range(usrc, n)) {
        return -EFAULT;
    }
    return copy(kdst, usrc, n) ? -EFAULT : 0;
}

int copy_to_user(void* udst, const void* ksrc, size_t n)
{
    if (!user_range(udst, n)) {
        return -EFAULT;
    }
    return copy(udst, ksrc, n) ? -EFAULT : 0;
}

long strncpy_from_user(char* kdst, const void* usrc, size_t max)
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
    csr_set(sstatus, SSTATUS_SUM);
    long len = uaccess_strncpy_bytes(kdst, usrc, n);
    csr_clear(sstatus, SSTATUS_SUM);
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
