// Lecture 12 - 64-bit paging types.
//
// Replaces the 32-bit paging.h (paging_4gb_chunk + uint32_t* directory
// pointers). In 64-bit long mode page table entries are 8 bytes each,
// and the walk has four levels (PML4 -> PDPT -> PD -> PT). We define
// the entry as a bit-field so individual fields read out cleanly, and
// the per-level container is just an array of 512 such entries.
//
// Function prototypes are intentionally NOT here yet. Lecture 13 lands
// paging_set / paging_get / paging_map / paging_load_directory and
// then paging.c gets compiled into the build.

#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

enum
{
    PAGING_MAP_LEVEL_4 = 4,
    // 5 is not yet supported (5-level paging is the future
    // architectural extension; 9 more bits of virtual address)
};
typedef uint8_t paging_map_level_t;

// Flag bits for a paging-descriptor entry (the same names the 32-bit
// SamOs used so call-sites can stay similar).
#define PAGING_CACHE_DISABLED   0b00010000
#define PAGING_WRITE_THROUGH    0b00001000
#define PAGING_ACCESS_FROM_ALL  0b00000100  // U=1: ring 3 can see this page
#define PAGING_IS_WRITEABLE     0b00000010
#define PAGING_IS_PRESENT       0b00000001

#define PAGING_TOTAL_ENTRIES_PER_TABLE  512
#define PAGING_PAGE_SIZE                4096

enum
{
    // ~512 GiB - all 512 PML4 entries fully populated.
    PAGING_PML4T_MAX_ADDRESSABLE = 0x8000000000ULL,
    // 1 GiB per PDPT entry.
    PAGING_PDPT_MAX_ADDRESSABLE  = 0x40000000ULL,
    // 2 MiB per PD entry.
    PAGING_PD_MAX_ADDRESSABLE    = 0x200000ULL,
    // 4 KiB per PT entry (== one page).
    PAGING_PAGE_MAX_ADDRESSABLE  = PAGING_PAGE_SIZE,
};

// One paging-descriptor entry. Bit layout matches Intel SDM Vol 3 for
// a PML4E / PDPTE / PDE / PTE in 4-level paging. Same packed struct
// for every level - the address field decodes differently at the PT
// level (page base) vs the intermediate levels (next-table base),
// but the bit positions are identical.
struct paging_desc_entry
{
    uint64_t present         : 1;   // Bit 0
    uint64_t read_write      : 1;   // Bit 1
    uint64_t user_supervisor : 1;   // Bit 2
    uint64_t pwt             : 1;   // Bit 3: PWT (page-level write-through)
    uint64_t pcd             : 1;   // Bit 4: PCD (page-level cache disable)
    uint64_t accessed        : 1;   // Bit 5
    uint64_t ignored         : 1;   // Bit 6 (dirty in a PTE)
    uint64_t reserved0       : 1;   // Bit 7: must be 0 in PML4E (PS in PDE)
    uint64_t reserved1       : 4;   // Bits 8..11 reserved
    uint64_t address         : 40;  // Bits 12..51: next-level base or page base
    uint64_t available       : 11;  // Bits 52..62 OS-available
    uint64_t execute_disable : 1;   // Bit 63: XD (requires EFER.NXE)
} __attribute__((packed));

// One full paging-descriptor table - 512 entries of paging_desc_entry.
struct paging_pml_entries
{
    struct paging_desc_entry entries[PAGING_TOTAL_ENTRIES_PER_TABLE];
} __attribute__((packed));

// A complete paging descriptor: the top-level table plus its level.
// SamOs always uses 4-level paging today; the level field anticipates
// 5-level (LA57) support without breaking the interface.
struct paging_desc
{
    struct paging_pml_entries* pml;
    paging_map_level_t         level;
} __attribute__((packed));

// ---------------------------------------------------------------
// 32-bit API kept here in comments for cross-reference with the
// SamOs main branch. Each one is replaced by a 64-bit equivalent
// in Lectures 13+:
//
// struct paging_4gb_chunk* paging_new_4gb(uint8_t flags);
// void  paging_switch(struct paging_4gb_chunk* directory);
// void  enable_paging();
// int   paging_set(uint32_t* directory, void* virt, uint32_t val);
// uint32_t paging_get(uint32_t* directory, void* virt);
// bool  paging_is_aligned(void* addr);
// void  paging_free_4gb(struct paging_4gb_chunk* chunk);
// int   paging_map_to(...);
// int   paging_map_range(...);
// int   paging_map(...);
// void* paging_align_address(void* ptr);
// void* paging_align_to_lower_page(void* addr);
// void* paging_get_physical_address(uint32_t* directory, void* virt);

#endif
