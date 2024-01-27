// Checked access to user memory. See uaccess.h.

#include "uaccess.h"
#include "process.h"
#include "config.h"
#include "status.h"
#include "memory/memory.h"
#include "arch/arch.h"

// Kernel address of the user byte at `uva`, or NULL if the process
// may not access it.
static char* uaccess_byte(struct process* process, uintptr_t uva, bool write)
{
    uint64_t phys = 0;
    if (!process || !process->paging_desc ||
        paging_user_translate(process->paging_desc, uva, write, &phys) < 0)
    {
        return NULL;
    }
    return PAGING_PHYS_TO_VIRT(phys);
}

static bool uaccess_range_valid(uintptr_t uva, size_t n)
{
    return uva < SAMOS_USER_SPACE_END && n <= SAMOS_USER_SPACE_END - uva;
}

static size_t uaccess_chunk(uintptr_t uva, size_t n)
{
    size_t chunk = PAGING_PAGE_SIZE - (uva & (PAGING_PAGE_SIZE - 1));
    return chunk < n ? chunk : n;
}

// Copy page by page, each page translated on its own (a
// buffer may cross into a page that is not mapped).
static int uaccess_copy(struct process* process, char* kbuf, uintptr_t uva, size_t n, bool to_user)
{
    if (n == 0)
    {
        return 0;
    }
    if (!uaccess_range_valid(uva, n))
    {
        return -EFAULT;
    }
    while (n)
    {
        size_t chunk = uaccess_chunk(uva, n);
        char* k = uaccess_byte(process, uva, to_user);
        if (!k)
        {
            return -EFAULT;
        }
        if (to_user)
        {
            memcpy(k, kbuf, (int)chunk);
        }
        else
        {
            memcpy(kbuf, k, (int)chunk);
        }
        kbuf += chunk;
        uva  += chunk;
        n    -= chunk;
    }
    return 0;
}

int copy_from_user(struct process* process, void* kdst, const void* usrc, size_t n)
{
    return uaccess_copy(process, kdst, (uintptr_t)usrc, n, false);
}

int copy_to_user(struct process* process, void* udst, const void* ksrc, size_t n)
{
    return uaccess_copy(process, (char*)ksrc, (uintptr_t)udst, n, true);
}

bool user_range_ok(struct process* process, const void* uptr, size_t n, bool write)
{
    uintptr_t uva = (uintptr_t)uptr;
    if (n == 0)
    {
        return true;
    }
    if (!uaccess_range_valid(uva, n))
    {
        return false;
    }
    while (n)
    {
        size_t chunk = uaccess_chunk(uva, n);
        if (!uaccess_byte(process, uva, write))
        {
            return false;
        }
        uva += chunk;
        n   -= chunk;
    }
    return true;
}

long strncpy_from_user(struct process* process, char* kdst, const void* usrc, size_t max)
{
    uintptr_t uva = (uintptr_t)usrc;
    if (max == 0)
    {
        return -ENAMETOOLONG;
    }
    char* k = NULL;
    for (size_t i = 0; i < max; i++, uva++)
    {
        if (!k || (uva & (PAGING_PAGE_SIZE - 1)) == 0)
        {
            k = uva < SAMOS_USER_SPACE_END ? uaccess_byte(process, uva, false) : NULL;
            if (!k)
            {
                kdst[i] = 0;
                return -EFAULT;
            }
        }
        char c = *k++;
        if (!c || i == max - 1)
        {
            kdst[i] = 0;
            if (!c)
            {
                return (long)i;
            }
            break;
        }
        kdst[i] = c;
    }
    return -ENAMETOOLONG;
}
