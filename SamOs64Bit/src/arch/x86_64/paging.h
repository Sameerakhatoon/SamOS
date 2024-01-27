// Lecture 13 - 64-bit paging API: types + function prototypes.
//
// The portable part of the API (paging_map, paging_switch, the
// PAGING_* flags, ...) is declared in arch/arch.h; this header adds
// the x86 page-table layout for the code under src/arch/x86_64/.

#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "arch/arch.h"

enum
{
    PAGING_MAP_LEVEL_4 = 4,
    // 5 is not yet supported.
};
typedef uint8_t paging_map_level_t;

// The upper half of a PML4: slots 256-511 are the kernel's.
#define PAGING_KERNEL_PML4_FIRST 256

#define PAGING_TOTAL_ENTRIES_PER_TABLE  512

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

int   paging_map_e820_memory_regions(struct paging_desc* desc);

// Lecture 29 prototype, L31 real implementation: walks PML4 ->
// PDPT -> PD -> PT and returns a pointer to the PT leaf entry,
// or NULL if any intermediate table entry is missing.
struct paging_desc_entry* paging_get(struct paging_desc* desc, void* virtual_address);

// Asm helpers (paging.asm).
void  paging_load_directory(uintptr_t* directory);
void  paging_invalidate_tlb_entry(void* addr);

#endif
