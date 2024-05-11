#ifndef ARCH_AARCH64_MMU_H
#define ARCH_AARCH64_MMU_H

#include <stdint.h>

// Stage 1 translation at EL1: 4 KiB granule, 48-bit VAs, four
// levels (L0-L3, 512 entries each). The x86 layout:
//
//   0x0000000000000000-0x0000ffffffffffff  TTBR0: user (struct aspace)
//   0xffff800000000000 + pa                TTBR1: direct map of RAM (RW, XN);
//                                          MMIO is mapped on demand at the
//                                          same offset as Device memory
//   0xffffffff80000000                     TTBR1: the kernel image, mapped to
//                                          wherever it was loaded (W^X)
//
// The kernel is linked at KERNEL_VBASE but entered with the MMU off at
// its load address. mmu_early (boot.S, MMU off) builds the final
// TTBR1 tables plus an identity map of the kernel text; boot.S turns
// the MMU on and jumps high; mmu_late (MMU on) drops the identity map.
// TTBR0 then points at an empty table until an aspace is switched in.

#define KERNEL_VBASE        0xffffffff80000000ul
#define DIRECT_MAP_BASE     0xffff800000000000ul
#define USER_TOP            0x0001000000000000ul
#define PAGE_SIZE           4096ul

// MAIR_EL1 attribute indexes.
#define MT_NORMAL           0       // 0xff: Normal, inner/outer write-back RW-allocate
#define MT_DEVICE           1       // 0x04: Device-nGnRE

// Descriptor bits.
#define PTE_VALID           (1ul << 0)
#define PTE_TABLE           (1ul << 1)      // L0-L2: next table; L3: page (with VALID)
#define PTE_ATTR(mt)        ((uint64_t)(mt) << 2)
#define PTE_AP_EL0          (1ul << 6)      // AP[1]: EL0 may access
#define PTE_AP_RO           (1ul << 7)      // AP[2]: read only (EL1 and EL0)
#define PTE_SH_INNER        (3ul << 8)
#define PTE_AF              (1ul << 10)     // no hardware AF on v8.0: set it or fault
#define PTE_NG              (1ul << 11)     // not global: tagged with the ASID
#define PTE_PXN             (1ul << 53)
#define PTE_UXN             (1ul << 54)
#define PTE_ADDR            0x0000fffffffff000ul
// A TLBI by VA operand: VA[55:12] in bits [43:0]. Bit 55
// picks TTBR1, so a 40-bit field would aim at a user address.
#define TLBI_VA(va)         (((uint64_t)(va) >> 12) & ((1ul << 44) - 1))

// Kernel mapping kinds (W^X: nothing is writable and executable).
#define PROT_KTEXT          (PTE_ATTR(MT_NORMAL) | PTE_SH_INNER | PTE_AP_RO | PTE_UXN)
#define PROT_KRO            (PTE_ATTR(MT_NORMAL) | PTE_SH_INNER | PTE_AP_RO | PTE_PXN | PTE_UXN)
#define PROT_KRW            (PTE_ATTR(MT_NORMAL) | PTE_SH_INNER | PTE_PXN | PTE_UXN)
#define PROT_DEVICE         (PTE_ATTR(MT_DEVICE) | PTE_PXN | PTE_UXN)

// User page permissions for aspace_map.
#define USER_R              0
#define USER_W              1
#define USER_X              2

// pa + mmu_dm_offset: 0 with the MMU off (identity), DIRECT_MAP_BASE after.
extern uint64_t mmu_dm_offset;
extern uint64_t kernel_phys;        // load address of _head

static inline void* phys_to_virt(uint64_t pa)
{
    return (void*)(pa + mmu_dm_offset);
}

static inline uint64_t virt_to_phys(const void* va)    // direct map only
{
    return (uint64_t)va - mmu_dm_offset;
}

// Physical address of a kernel image symbol, with the MMU on or off.
static inline uint64_t kimage_pa(const void* p)
{
    return mmu_dm_offset ? (uint64_t)p - KERNEL_VBASE + kernel_phys : (uint64_t)p;
}

// boot.S, MMU off: memory map, PFA, tables, MAIR/TCR. Halts on error.
void     mmu_early(uint64_t arg);     // x0: the DTB, or the UEFI boot_info
// boot.S, MMU on and running high: direct map on, identity map gone.
void     mmu_late(void);
// Print the layout, the reservations and the allocator (main.c).
void     mmu_report(void);
// Boot.S's values for a secondary CPU's MMU (an identity map of
// the kernel text again, this CPU's SCTLR, TCR, MAIR, TTBR1): before
// the first PSCI CPU_ON, 0 or -1; and the identity map freed once
// every CPU started has left it.
int      mmu_secondary_prepare(void);
void     mmu_secondary_done(void);

// Device memory at DIRECT_MAP_BASE + pa (XN), returns that address;
// 0 if it cannot be mapped. With the MMU off, returns pa.
uint64_t mmu_map_device(uint64_t pa, uint64_t size);
// Kernel mapping in TTBR1 with any descriptor bits (test hooks).
int      mmu_map_kernel(uint64_t va, uint64_t pa, uint64_t size, uint64_t prot);
// Leaf descriptor for va in the kernel (va >= 2^63) or the current
// user table, 0 if unmapped; *level is its level.
uint64_t mmu_lookup(uint64_t va, int* level);

// PAR_EL1 after an AT instruction: bit 0 set = fault, FST in [6:1];
// else the PA in [47:12].
#define AT_S1E1R 0
#define AT_S1E1W 1
#define AT_S1E0R 2
#define AT_S1E0W 3
uint64_t mmu_at(uint64_t va, int kind);

// ---- user address spaces (TTBR0) --------------------------------
// One L0 table per space and an 8-bit ASID (1-255; 0 is "no user
// space"). User pages are non-global, so switching TTBR0 needs no
// TLB flush; aspace_destroy flushes its ASID before reusing it.
struct aspace {
    uint64_t root;      // pa of the L0 table
    uint32_t asid;
};

int      aspace_create(struct aspace* as);             // 0, or -1 (no ASID or memory)
int      aspace_map(struct aspace* as, uint64_t va, uint64_t pa, int prot);
void     aspace_switch(const struct aspace* as);       // NULL: no user space
// Frees the tables (not the mapped frames) and the ASID; switches
// away first if it is the current space.
void     aspace_destroy(struct aspace* as);

uint64_t mmu_table_frames(void);    // page-table frames in use

// ---- what paging.c (arch.h's paging_*) builds on ----------
// Table frames from the shared PFA from now on (kernel_main's
// paging_kernel_desc_new, after kheap_init).
void     mmu_use_shared_pfa(void);
uint64_t mmu_kernel_root(void);                         // pa of the TTBR1 L0 table
uint64_t mmu_lookup_root(uint64_t root, uint64_t va, int* level);
// One 4 KiB L3 entry: `desc` (0 unmaps), break-before-make with a
// TLB flush for `asid` (or every ASID for a global entry). 0 or -1.
int      mmu_set_page(uint64_t root, uint64_t va, uint64_t desc, uint32_t asid);
void     mmu_sync_icache(uint64_t pa, uint64_t size);
uint64_t aspace_ttbr0(const struct aspace* as);         // TTBR0_EL1 value (NULL: empty)

#endif
