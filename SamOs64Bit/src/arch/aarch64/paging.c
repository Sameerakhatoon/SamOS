// arch.h's MMU half on aarch64: the shared paging_* calls over
// the aarch64 MMU code's tables (mmu.c).
//
// A paging_desc is a user address space (TTBR0, struct aspace with its
// ASID) or, for the kernel's own descriptor, no user space at all
// (TTBR0 = the empty table). The kernel half is TTBR1, one tree every
// descriptor shares, so a map of an address with bit 63 set goes there
// whatever descriptor it names (the multiheap arena, the isolation code's kpalloc).
//
// The portable PAGING_* flags become descriptor bits here:
//   PRESENT         valid page, AF set (no hardware access flag on v8.0)
//   WRITEABLE       AP[2] clear (else read-only at every level)
//   ACCESS_FROM_ALL AP[1] set: EL0 may access (user half only)
//   NO_EXECUTE      UXN for user pages, PXN for kernel pages; user pages
//                   are always PXN, kernel pages always UXN
//   CACHE_DISABLED  Device-nGnRE (MAIR attr 1)
// User pages are non-global (nG), so they are tagged with the ASID.

#include "arch/arch.h"
#include "arch/aarch64/memmap.h"
#include "arch/aarch64/mmu.h"
#include "cpu/percpu.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "status.h"

struct paging_desc {
    struct aspace as;       // root 0 for the kernel's descriptor
    bool          kernel;
};

static bool kernel_half(uint64_t va)
{
    return va >> 63;
}

static uint64_t leaf_bits(uint64_t va, int flags)
{
    bool user = !kernel_half(va);
    uint64_t d = PTE_VALID | PTE_TABLE | PTE_AF;
    if (flags & PAGING_CACHE_DISABLED)
        d |= PTE_ATTR(MT_DEVICE);
    else
        d |= PTE_ATTR(MT_NORMAL) | PTE_SH_INNER;
    if (!(flags & PAGING_IS_WRITEABLE))
        d |= PTE_AP_RO;
    if (user) {
        d |= PTE_NG | PTE_PXN;
        if (flags & PAGING_ACCESS_FROM_ALL)
            d |= PTE_AP_EL0;
        if (flags & PAGING_NO_EXECUTE)
            d |= PTE_UXN;
    } else {
        d |= PTE_UXN;
        if (flags & PAGING_NO_EXECUTE)
            d |= PTE_PXN;
    }
    return d;
}

// Leaf entry for va and the physical address of the byte; 0 if unmapped.
static uint64_t walk(struct paging_desc* desc, uint64_t va, uint64_t* pa)
{
    uint64_t root = kernel_half(va) ? mmu_kernel_root() : (desc && !desc->kernel ? desc->as.root : 0);
    if (!root)
        return 0;
    int level = 3;
    uint64_t e = mmu_lookup_root(root, va, &level);
    if (!e)
        return 0;
    uint64_t size = 1ul << (39 - 9 * level);
    if (pa)
        *pa = (e & PTE_ADDR & ~(size - 1)) | (va & (size - 1));
    return e;
}

struct paging_desc* paging_kernel_desc_new(void)
{
    struct paging_desc* d = kzalloc(sizeof(*d));
    if (d)
        d->kernel = true;
    // The shared PFA exists now (kheap_init): table frames come from it.
    mmu_use_shared_pfa();
    return d;
}

struct paging_desc* paging_desc_new(void)
{
    struct paging_desc* d = kzalloc(sizeof(*d));
    if (!d)
        return NULL;
    kmem_lock();
    int r = aspace_create(&d->as);
    kmem_unlock();
    if (r < 0) {
        kfree(d);
        return NULL;
    }
    return d;
}

void paging_desc_free(struct paging_desc* desc)
{
    if (!desc)
        return;
    if (!desc->kernel) {
        if (this_cpu()->paging_desc == desc)
            this_cpu()->paging_desc = NULL;
        kmem_lock();
        aspace_destroy(&desc->as);
        kmem_unlock();
    }
    kfree(desc);
}

void paging_switch(struct paging_desc* desc)
{
    this_cpu()->paging_desc = desc;
    aspace_switch(desc && !desc->kernel ? &desc->as : NULL);
}

struct paging_desc* paging_current_descriptor(void)
{
    return this_cpu()->paging_desc;
}

int paging_desc_levels(struct paging_desc* desc)
{
    return 4;
}

int paging_map(struct paging_desc* desc, void* virt, void* phys, int flags)
{
    uint64_t va = (uint64_t)virt, pa = (uint64_t)phys;
    if ((va | pa) & (PAGING_PAGE_SIZE - 1))
        return -EINVARG;
    bool kernel = kernel_half(va);
    if (kernel ? (flags & PAGING_ACCESS_FROM_ALL) : (!desc || desc->kernel || va >= USER_TOP))
        return -EINVARG;
    uint64_t d = flags & PAGING_IS_PRESENT ? (pa & PTE_ADDR) | leaf_bits(va, flags) : 0;
    // Table frames come from the PFA, which takes the kernel memory
    // lock too (recursive); one CPU at a time in a tree.
    kmem_lock();
    int r = mmu_set_page(kernel ? mmu_kernel_root() : desc->as.root, va, d, kernel ? 0 : desc->as.asid);
    kmem_unlock();
    if (r < 0)
        return -ENOMEM;
    if (d && !kernel && !(flags & PAGING_NO_EXECUTE))
        mmu_sync_icache(pa, PAGING_PAGE_SIZE);
    return 0;
}

int paging_map_range(struct paging_desc* desc, void* virt, void* phys, size_t count, int flags)
{
    for (size_t i = 0; i < count; i++) {
        int res = paging_map(desc, (char*)virt + i * PAGING_PAGE_SIZE,
                             (char*)phys + i * PAGING_PAGE_SIZE, flags);
        if (res < 0)
            return res;
    }
    return 0;
}

int paging_map_to(struct paging_desc* desc, void* virt, void* phys, void* phys_end, int flags)
{
    if (((uintptr_t)virt | (uintptr_t)phys | (uintptr_t)phys_end) % PAGING_PAGE_SIZE ||
        (uintptr_t)phys_end < (uintptr_t)phys)
        return -EINVARG;
    return paging_map_range(desc, virt, phys, ((uintptr_t)phys_end - (uintptr_t)phys) / PAGING_PAGE_SIZE,
                            flags);
}

void* paging_get_physical_address(struct paging_desc* desc, void* virt)
{
    uint64_t pa;
    return walk(desc, (uint64_t)virt, &pa) ? (void*)pa : NULL;
}

int paging_get_flags(struct paging_desc* desc, void* virt)
{
    uint64_t va = (uint64_t)virt;
    uint64_t e = walk(desc, va, NULL);
    if (!e)
        return 0;
    int flags = PAGING_IS_PRESENT;
    if (!(e & PTE_AP_RO))
        flags |= PAGING_IS_WRITEABLE;
    if (e & PTE_AP_EL0)
        flags |= PAGING_ACCESS_FROM_ALL;
    if (e & (kernel_half(va) ? PTE_PXN : PTE_UXN))
        flags |= PAGING_NO_EXECUTE;
    if (((e >> 2) & 7) == MT_DEVICE)
        flags |= PAGING_CACHE_DISABLED;
    return flags;
}

uint64_t paging_kernel_virt_to_phys(void* virt)
{
    uint64_t pa;
    return kernel_half((uint64_t)virt) && walk(NULL, (uint64_t)virt, &pa) ? pa : 0;
}

// A cacheable request inside RAM (ACPI tables) is the direct
// map: Normal memory, where the ACPI code's unaligned loads are fine
// (on Device memory they fault), and no lock, so acpi_init works in
// aarch64_main before this CPU's struct cpu exists.
void* paging_map_mmio(uint64_t phys, uint64_t size, int flags)
{
    if (!(flags & PAGING_CACHE_DISABLED) && memmap_ram(phys, size))
        return phys_to_virt(phys);
    kmem_lock();
    uint64_t va = mmu_map_device(phys, size);
    kmem_unlock();
    return (void*)va;
}

// The uaccess contract: mapped, EL0 accessible, and writable
// for a write.
int paging_user_translate(struct paging_desc* desc, uintptr_t virt, bool write, uint64_t* phys_out)
{
    uint64_t pa;
    if (!desc || desc->kernel || virt >= USER_TOP)
        return -EFAULT;
    uint64_t e = walk(desc, virt, &pa);
    if (!e || !(e & PTE_AP_EL0) || (write && (e & PTE_AP_RO)))
        return -EFAULT;
    *phys_out = pa;
    return 0;
}

// TTBR0_EL1 value of a descriptor (uaccess.c switches to it).
uint64_t paging_desc_ttbr0(struct paging_desc* desc)
{
    return aspace_ttbr0(desc && !desc->kernel ? &desc->as : NULL);
}

void arch_tlb_flush_local(uint64_t va)
{
    if (va)
        __asm__ volatile("dsb nshst\n\ttlbi vaae1, %0\n\tdsb nsh\n\tisb" :: "r"((va >> 12) & 0xfffffffffful) : "memory");
    else
        __asm__ volatile("dsb nshst\n\ttlbi vmalle1\n\tdsb nsh\n\tisb" ::: "memory");
}

void* paging_align_address(void* ptr)
{
    return (void*)paging_align_value_to_upper_page((uint64_t)ptr);
}

void* paging_align_to_lower_page(void* addr)
{
    return (void*)((uintptr_t)addr & ~(uintptr_t)(PAGING_PAGE_SIZE - 1));
}

uint64_t paging_align_value_to_upper_page(uint64_t val)
{
    return (val + PAGING_PAGE_SIZE - 1) & ~(uint64_t)(PAGING_PAGE_SIZE - 1);
}

bool paging_is_aligned(void* addr)
{
    return ((uintptr_t)addr % PAGING_PAGE_SIZE) == 0;
}
