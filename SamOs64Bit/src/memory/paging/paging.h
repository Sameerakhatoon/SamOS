// Lecture 13 - 64-bit paging API: types + function prototypes.

#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

enum
{
    PAGING_MAP_LEVEL_4 = 4,
    // 5 is not yet supported.
};
typedef uint8_t paging_map_level_t;

#include "config.h"

#define PAGING_NO_EXECUTE       0x100       // NX (bit 63 of the entry)
#define PAGING_CACHE_DISABLED   0b00010000
#define PAGING_WRITE_THROUGH    0b00001000
#define PAGING_ACCESS_FROM_ALL  0b00000100  // U=1: ring 3 can see this page
#define PAGING_IS_WRITEABLE     0b00000010
#define PAGING_IS_PRESENT       0b00000001

// The direct map: physical address p is kernel virtual
// SAMOS_DIRECT_MAP_BASE + p. Every kmalloc pointer and every page
// table is a direct-map address; page-table entries hold physical ones.
#define PAGING_PHYS_TO_VIRT(p)  ((void*)((uintptr_t)(p) + SAMOS_DIRECT_MAP_BASE))
#define PAGING_IS_DIRECT_MAP(v) ((uintptr_t)(v) >= SAMOS_DIRECT_MAP_BASE && \
                                 (uintptr_t)(v) <  SAMOS_DIRECT_MAP_BASE + SAMOS_DIRECT_MAP_SIZE)

// The upper half of a PML4: slots 256-511 are the kernel's.
#define PAGING_KERNEL_PML4_FIRST 256

#define PAGING_TOTAL_ENTRIES_PER_TABLE  512
#define PAGING_PAGE_SIZE                4096

enum
{
    PAGING_PML4T_MAX_ADDRESSABLE = 0x8000000000ULL, // ~512 GiB
    PAGING_PDPT_MAX_ADDRESSABLE  = 0x40000000ULL,   // 1 GiB
    PAGING_PD_MAX_ADDRESSABLE    = 0x200000ULL,     // 2 MiB
    PAGING_PAGE_MAX_ADDRESSABLE  = PAGING_PAGE_SIZE,
};

// One paging-descriptor entry. Intel SDM Vol 3 layout for PML4E /
// PDPTE / PDE / PTE in 4-level paging - same 8-byte word at every
// level; only the meaning of the 40-bit address field changes.
struct paging_desc_entry
{
    uint64_t present         : 1;   // Bit 0
    uint64_t read_write      : 1;   // Bit 1
    uint64_t user_supervisor : 1;   // Bit 2
    uint64_t pwt             : 1;   // Bit 3
    uint64_t pcd             : 1;   // Bit 4
    uint64_t accessed        : 1;   // Bit 5
    uint64_t ignored         : 1;   // Bit 6 (dirty in PTE)
    uint64_t reserved0       : 1;   // Bit 7 (PS in PDE)
    uint64_t reserved1       : 4;   // 8..11
    uint64_t address         : 40;  // 12..51 (page-base or next-table base)
    uint64_t available       : 11;  // 52..62
    uint64_t execute_disable : 1;   // 63
} __attribute__((packed));

struct paging_pml_entries
{
    struct paging_desc_entry entries[PAGING_TOTAL_ENTRIES_PER_TABLE];
} __attribute__((packed));

struct paging_desc
{
    struct paging_pml_entries* pml;
    paging_map_level_t         level;
} __attribute__((packed));

// C-side mapping API (L13 onward).
int   paging_map_to(struct paging_desc* desc, void* virt, void* phys, void* phys_end, int flags);
int   paging_map_range(struct paging_desc* desc, void* virt, void* phys, size_t count, int flags);
int   paging_map(struct paging_desc* desc, void* virt, void* phys, int flags);
void* paging_align_to_lower_page(void* addr);
void* paging_align_address(void* ptr);
struct paging_desc* paging_desc_new(paging_map_level_t root_map_level);
void  paging_switch(struct paging_desc* desc);
// Lecture 43 - recursive teardown of a 4-level paging tree.
// Frees intermediate tables (PML4 entries' PDPTs, PDs, PTs) but
// NOT the 4-KiB pages mapped by the PT leaves - those are user
// data, owned by whoever allocated them.
void  paging_desc_free(struct paging_desc* desc);
bool  paging_is_aligned(void* addr);
struct paging_desc* paging_current_descriptor(void);
int   paging_map_e820_memory_regions(struct paging_desc* desc);

// Lecture 29 - virtual-to-physical translation, used by the new
// multiheap_free path. Walks the page tables in `desc` until
// it finds the PT entry for `virtual_address` and recombines
// address-base + offset-within-page. Returns NULL when the
// walk terminates on a missing entry.
void* paging_get_physical_address(struct paging_desc* desc, void* virtual_address);

// Lecture 29 prototype, L31 real implementation: walks PML4 ->
// PDPT -> PD -> PT and returns a pointer to the PT leaf entry,
// or NULL if any intermediate table entry is missing.
struct paging_desc_entry* paging_get(struct paging_desc* desc, void* virtual_address);

// Kernel address space and translation.
// paging_kernel_desc_new builds the kernel descriptor: every upper
// half PML4 slot, the direct map and the kernel image with W^X.
// paging_desc_new then shares those slots with each new descriptor.
struct paging_desc* paging_kernel_desc_new(void);
// Physical address behind a kernel virtual address (direct map,
// kernel image, else a walk of the kernel tables). 0 if unmapped.
uint64_t paging_kernel_virt_to_phys(void* virt);
// Map [phys, phys + size) into the direct map with `flags`
// (MMIO: PCI BARs). Returns the kernel virtual address or NULL.
void* paging_map_mmio(uint64_t phys, uint64_t size, int flags);
// Walk `desc` for a user access to `virt`: every level present and
// U=1, and RW=1 at every level when `write`. Returns 0 and the
// physical address of the byte, or -EFAULT.
int   paging_user_translate(struct paging_desc* desc, uintptr_t virt, bool write, uint64_t* phys_out);

// Asm helpers (paging.asm).
void  paging_load_directory(uintptr_t* directory);
void  paging_invalidate_tlb_entry(void* addr);

// Lecture 166 - round val_in up to the next page boundary.
// Returns val_in unchanged when already page-aligned.
uint64_t paging_align_value_to_upper_page(uint64_t val_in);

#endif
