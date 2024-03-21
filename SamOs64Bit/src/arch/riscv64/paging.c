// arch.h's paging_* on riscv64, over the riscv64 MMU code's tables (mmu.c).
//
// A paging_desc is a riscv64 MMU struct mm: its own root (lower half) with
// the kernel's root-level upper-half entries copied in, and an ASID.
// The kernel's descriptor is the boot root itself. The portable
// PAGING_* bits become PTE bits: present R, writeable W, no NO_EXECUTE
// X, ACCESS_FROM_ALL U; kernel-half leaves are global.

#include "arch/arch.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/riscv64.h"
#include "cpu/percpu.h"
#include "memory/heap/kheap.h"
#include "status.h"

struct paging_desc {
    struct mm mm;
    bool      kernel;
};

static struct paging_desc kernel_paging;

struct paging_desc* paging_kernel_desc_new(void)
{
    kernel_paging.mm.root = mmu_kernel_root();
    kernel_paging.mm.asid = 0;
    kernel_paging.kernel = true;
    return &kernel_paging;
}

struct paging_desc* paging_desc_new(void)
{
    struct paging_desc* d = kzalloc(sizeof(*d));
    if (d && mm_create(&d->mm) != 0) {
        kfree(d);
        d = NULL;
    }
    return d;
}

void paging_desc_free(struct paging_desc* desc)
{
    if (!desc || desc->kernel) {
        return;
    }
    if (this_cpu()->paging_desc == desc) {
        paging_switch(&kernel_paging);
    }
    mm_destroy(&desc->mm);
    kfree(desc);
}

void paging_switch(struct paging_desc* desc)
{
    this_cpu()->paging_desc = desc;
    mm_switch(desc->kernel ? NULL : &desc->mm);
}

struct paging_desc* paging_current_descriptor(void)
{
    return this_cpu()->paging_desc;
}

int paging_desc_levels(struct paging_desc* desc)
{
    return mmu_levels;
}

static bool kernel_half(uint64_t va)
{
    return va >= dmap_base;
}

static uint64_t pte_flags(uint64_t va, int flags)
{
    uint64_t pte = PTE_R | PTE_A;
    if (flags & PAGING_IS_WRITEABLE) {
        pte |= PTE_W | PTE_D;
    }
    if (!(flags & PAGING_NO_EXECUTE)) {
        pte |= PTE_X;
    }
    if (kernel_half(va)) {
        pte |= PTE_G;
    } else if (flags & PAGING_ACCESS_FROM_ALL) {
        pte |= PTE_U;
    }
    return pte;
}

// One 4 KiB page. Flags without PAGING_IS_PRESENT (x86's "flags 0,
// phys NULL") unmap it. User pages only below user_end, kernel pages
// only in the upper half; no W+X in the kernel half.
int paging_map(struct paging_desc* desc, void* virt, void* phys, int flags)
{
    uint64_t va = (uint64_t)virt, pa = (uint64_t)phys;
    if ((va | pa) & (PAGE_SIZE - 1)) {
        return -EINVARG;
    }
    bool kva = kernel_half(va);
    if ((!kva && va >= user_end) || (kva && (flags & PAGING_ACCESS_FROM_ALL))) {
        return -EINVARG;
    }
    bool present = flags & PAGING_IS_PRESENT;
    uint64_t pte = pte_flags(va, flags);
    if (present && kva && (pte & PTE_W) && (pte & PTE_X)) {
        return -EINVARG;
    }
    if (kva) {
        kmem_lock();   // the kernel half is shared (docs/locking.md)
    }
    uint64_t* e = pt_entry(desc->mm.root, va, 0, present);
    int res = 0;
    if (e) {
        *e = present ? ((pa >> 2) & 0x003ffffffffffc00ul) | pte | PTE_V : 0;
    } else if (present) {
        res = -ENOMEM;
    }
    if (kva) {
        kmem_unlock();
        __asm__ volatile("sfence.vma %0, zero" : : "r"(va) : "memory");
    } else {
        mm_flush_page(&desc->mm, va);
    }
    return res;
}

int paging_map_range(struct paging_desc* desc, void* virt, void* phys, size_t count, int flags)
{
    for (size_t i = 0; i < count; i++) {
        int res = paging_map(desc, (char*)virt + i * PAGE_SIZE, (char*)phys + i * PAGE_SIZE, flags);
        if (res < 0) {
            return res;
        }
    }
    return 0;
}

int paging_map_to(struct paging_desc* desc, void* virt, void* phys, void* phys_end, int flags)
{
    if ((uint64_t)phys_end < (uint64_t)phys) {
        return -EINVARG;
    }
    return paging_map_range(desc, virt, phys, ((uint64_t)phys_end - (uint64_t)phys) / PAGE_SIZE, flags);
}

// Megapages and gigapages too (the direct map): the offset within the
// leaf is added back.
void* paging_get_physical_address(struct paging_desc* desc, void* virt)
{
    int level = 0;
    uint64_t pte = pt_lookup(desc->mm.root, (uint64_t)virt, &level);
    if (!pte) {
        return NULL;
    }
    uint64_t size = 1ul << (12 + 9 * level);
    return (void*)(((pte & 0x003ffffffffffc00ul) << 2) + ((uint64_t)virt & (size - 1)));
}

int paging_get_flags(struct paging_desc* desc, void* virt)
{
    uint64_t pte = pt_lookup(desc->mm.root, (uint64_t)virt, 0);
    if (!pte) {
        return 0;
    }
    int flags = PAGING_IS_PRESENT;
    flags |= pte & PTE_W ? PAGING_IS_WRITEABLE : 0;
    flags |= pte & PTE_X ? 0 : PAGING_NO_EXECUTE;
    flags |= pte & PTE_U ? PAGING_ACCESS_FROM_ALL : 0;
    return flags;
}

uint64_t paging_kernel_virt_to_phys(void* virt)
{
    uint64_t va = (uint64_t)virt;
    if (PAGING_IS_DIRECT_MAP(va)) {
        return va - dmap_base;
    }
    if (va >= KERNEL_VMA) {
        return va - kernel_va_off;
    }
    return (uint64_t)paging_get_physical_address(&kernel_paging, virt);
}

// RAM is already in the direct map; device registers go into the mmio
// window, each call a new mapping.
void* paging_map_mmio(uint64_t phys, uint64_t size, int flags)
{
    int level;
    if (pt_lookup(mmu_kernel_root(), dmap_base + phys, &level)) {
        return phys_to_virt(phys);
    }
    return (void*)mmu_map_mmio(phys, size);
}

// The uaccess contract's check: a U leaf, readable, and
// writable for a write. Translation only, no access.
int paging_user_translate(struct paging_desc* desc, uintptr_t virt, bool write, uint64_t* phys_out)
{
    if (!desc || virt >= user_end) {
        return -EFAULT;
    }
    int level = 0;
    uint64_t pte = pt_lookup(desc->mm.root, virt, &level);
    if (!pte || !(pte & PTE_U) || !(pte & PTE_R) || (write && !(pte & PTE_W))) {
        return -EFAULT;
    }
    uint64_t size = 1ul << (12 + 9 * level);
    *phys_out = ((pte & 0x003ffffffffffc00ul) << 2) + (virt & (size - 1));
    return 0;
}

void arch_tlb_flush_local(uint64_t va)
{
    if (va) {
        __asm__ volatile("sfence.vma %0, zero" : : "r"(va) : "memory");
    } else {
        sfence_vma();
    }
}

void* paging_align_address(void* ptr)
{
    return (void*)(((uint64_t)ptr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
}

void* paging_align_to_lower_page(void* addr)
{
    return (void*)((uint64_t)addr & ~(PAGE_SIZE - 1));
}

uint64_t paging_align_value_to_upper_page(uint64_t val)
{
    return (val + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

bool paging_is_aligned(void* addr)
{
    return ((uint64_t)addr & (PAGE_SIZE - 1)) == 0;
}
