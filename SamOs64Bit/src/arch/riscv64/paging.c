// arch.h's paging_* on riscv64, over the riscv64 MMU code's tables (mmu.c).
//
// A paging_desc is a riscv64 MMU struct mm: its own root (lower half) with
// the kernel's root-level upper-half entries copied in, and an ASID.
// The kernel's descriptor is the boot root itself. The portable
// PAGING_* bits become PTE bits: present R, writeable W, no NO_EXECUTE
// X, ACCESS_FROM_ALL U; kernel-half leaves are global.
//
// A changed or removed leaf is flushed on the other harts too,
// by SBI RFENCE (riscv64_tlb_shootdown below) instead of x86's
// smp_tlb_shootdown IPI. With ASIDs a hart keeps an address
// space's entries after switching away from it, so a user page is
// flushed on every hart that has ever loaded its descriptor (`harts`),
// not only on those running it now.

#include "arch/arch.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/riscv64.h"
#include "arch/riscv64/sbi.h"
#include "cpu/percpu.h"
#include "kernel.h"
#include "memory/heap/kheap.h"
#include "status.h"

struct paging_desc {
    struct mm mm;
    bool      kernel;
    volatile uint32_t harts;   // Bit n: cpus[n] has loaded it
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
    // Its ASID out of every TLB that held it, not only this one.
    riscv64_tlb_shootdown(desc, 0);
    mm_destroy(&desc->mm);
    kfree(desc);
}

// The bit before satp, with a full fence (the amo's .aqrl): a
// hart editing the tables either sees it, or edited them before this
// hart could load any of their entries (task_switch's rule).
void paging_switch(struct paging_desc* desc)
{
    struct cpu* c = this_cpu();
    c->paging_desc = desc;
    if (!desc->kernel && !(desc->harts & 1u << c->index)) {
        __atomic_fetch_or(&desc->harts, 1u << c->index, __ATOMIC_SEQ_CST);
    }
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
    uint64_t* e = present ? pt_entry_split(desc->mm.root, va) : pt_entry(desc->mm.root, va, 0, false);
    int res = 0;
    bool was_mapped = e && (*e & PTE_V);
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
    // An entry another hart may cache (x86's SMP rule; an
    // invalid one is never cached by QEMU).
    if (was_mapped) {
        riscv64_tlb_shootdown(desc, va);
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

// The shootdown through SBI RFENCE. The targets: every other
// online hart for a kernel-half page, else the others that have loaded
// `desc`. A remote fence is done in M-mode, so it reaches a hart
// spinning with interrupts off, and OpenSBI returns once every target
// has fenced. Hart ids may be anywhere: one call per 64-hart block of
// ids. va 0 flushes all of `desc` (its ASID), or everything for the
// kernel's. ipi_tlb counts the flushes each hart was sent, as x86
// counts its shootdown IPIs (syscall 51, the SMP self-test).
void riscv64_tlb_shootdown(struct paging_desc* desc, uint64_t va)
{
    struct cpu* me = this_cpu();
    if (!smp_active()) {
        return;
    }
    bool all = desc->kernel || kernel_half(va);
    uint32_t todo = 0;
    for (int i = 0; i < SAMOS_MAX_CPUS; i++) {
        if (&cpus[i] != me && cpus[i].online && (all || (desc->harts & 1u << i))) {
            todo |= 1u << i;
        }
    }
    uint64_t start = va & ~(PAGE_SIZE - 1), size = va ? PAGE_SIZE : ~0ul;
    uint16_t asid = all || !mmu_asid_bits() ? 0 : desc->mm.asid;
    smp_mb();   // the PTE store before any target walks again
    // (No __builtin_ctz: without Zbb it is a libgcc call, the riscv64 device code's
    // __bswapsi2 note in arch.c.)
    for (int first = 0; todo; first++) {
        if (!(todo & 1u << first)) {
            continue;
        }
        uint64_t base = cpus[first].arch.hart_id & ~63ul, mask = 0;
        for (int i = first; i < SAMOS_MAX_CPUS; i++) {
            if ((todo & 1u << i) && (cpus[i].arch.hart_id & ~63ul) == base) {
                mask |= 1ul << (cpus[i].arch.hart_id & 63);
                todo &= ~(1u << i);
                atomic_inc64(&cpus[i].ipi_tlb);
            }
        }
        long err = asid ? sbi_remote_sfence_vma_asid(mask, base, start, size, asid)
                        : sbi_remote_sfence_vma(mask, base, start, size);
        if (err) {
            panic("riscv64: SBI remote sfence.vma failed\n");
        }
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
