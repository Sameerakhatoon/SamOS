// Lecture 13 - C-side 64-bit paging.
//
// The static page tables in kernel.asm cover the bring-up identity
// map. From kernel_main onward we use paging_desc_new() to build
// new 4-level descriptor trees at runtime (one per process later),
// paging_map* to install ranges in them, paging_switch() to load
// CR3 with the new top-level table.

#include "paging.h"
#include "memory/heap/kheap.h"
#include "memory/heap/heap.h"
#include "memory/memory.h"
#include "status.h"

static struct paging_desc* current_paging_desc = 0;

struct paging_pml_entries* paging_pml4_entries_new(void)
{
    return kzalloc(sizeof(struct paging_pml_entries));
}

static bool paging_map_level_is_valid(paging_map_level_t level)
{
    return level == PAGING_MAP_LEVEL_4;  // level 5 not yet supported
}

void* paging_align_address(void* ptr)
{
    if ((uintptr_t)ptr % PAGING_PAGE_SIZE)
    {
        return (void*)((uintptr_t)ptr + PAGING_PAGE_SIZE - ((uintptr_t)ptr % PAGING_PAGE_SIZE));
    }
    return ptr;
}

void* paging_align_to_lower_page(void* addr)
{
    uintptr_t a = (uintptr_t)addr;
    a -= (a % PAGING_PAGE_SIZE);
    return (void*)a;
}

// Lecture 27 - expose the active descriptor so multiheap can
// queue page-table edits against the live PML4 without each
// caller having to thread a desc pointer through.
struct paging_desc* paging_current_descriptor(void){
    return current_paging_desc;
}

// Lecture 29 - STUB. paging_get returns the PT leaf entry for a
// given virtual address by walking PML4 -> PDPT -> PD -> PT.
// Real implementation lands in L31. Until then we return NULL,
// which propagates up through paging_get_physical_address and
// out of multiheap_free's virtual-arena branch. multiheap_free
// is dormant at L29 so this stub never actually fires.
struct paging_desc_entry* paging_get(struct paging_desc* desc, void* virtual_address){
    (void)desc;
    (void)virtual_address;
    return NULL;
}

// Lecture 29 - virtual-to-physical helper. Uses paging_get to
// find the leaf entry, reconstructs the physical address as
// (entry.address << 12) + (virtual_address & 0xFFF). Returns
// NULL when the walk fails (which is also what the L29 stub
// version of paging_get always returns).
void* paging_get_physical_address(struct paging_desc* desc, void* virtual_address){
    struct paging_desc_entry* entry = paging_get(desc, virtual_address);
    if(!entry){
        return NULL;
    }
    uint64_t physical_base = ((uint64_t)entry->address) << 12;
    uint64_t offset        = ((uint64_t)virtual_address) & 0xFFF;
    return (void*)(physical_base + offset);
}

void paging_switch(struct paging_desc* desc)
{
    current_paging_desc = desc;
    paging_load_directory((uintptr_t*)(&desc->pml->entries[0]));
}

struct paging_desc* paging_desc_new(paging_map_level_t root_map_level)
{
    if (!paging_map_level_is_valid(root_map_level))
    {
        return NULL;
    }
    struct paging_desc* desc = kzalloc(sizeof(struct paging_desc));
    if (!desc)
    {
        return NULL;
    }
    desc->pml   = paging_pml4_entries_new();
    desc->level = root_map_level;
    return desc;
}

bool paging_is_aligned(void* addr)
{
    return ((uintptr_t)addr % PAGING_PAGE_SIZE) == 0;
}

static bool paging_null_entry(struct paging_desc_entry* entry)
{
    struct paging_desc_entry null_desc = { 0 };
    return memcmp(entry, &null_desc, sizeof(struct paging_desc_entry)) == 0;
}

// Install one virt -> phys 4-KiB mapping. Allocates missing
// intermediate tables (PDPT, PD, PT) on demand from kheap.
int paging_map(struct paging_desc* desc, void* virt, void* phys, int flags)
{
    int res = 0;
    uintptr_t va = (uintptr_t)virt;

    // 48-bit canonical virtual address layout:
    //   bits 47..39 PML4 index
    //   bits 38..30 PDPT index
    //   bits 29..21 PD index
    //   bits 20..12 PT index
    //   bits 11..0  offset within the 4-KiB page
    size_t pml4_index = (va >> 39) & 0x1FF;
    size_t pdpt_index = (va >> 30) & 0x1FF;
    size_t pd_index   = (va >> 21) & 0x1FF;
    size_t pt_index   = (va >> 12) & 0x1FF;

    struct paging_desc_entry* pml4_entry = &desc->pml->entries[pml4_index];
    if (paging_null_entry(pml4_entry))
    {
        void* new_pdpt = kzalloc(
            sizeof(struct paging_desc_entry) * PAGING_TOTAL_ENTRIES_PER_TABLE);
        pml4_entry->address    = ((uintptr_t)new_pdpt) >> 12;
        pml4_entry->present    = 1;
        pml4_entry->read_write = 1;
    }

    struct paging_desc_entry* pdpt_entries =
        (struct paging_desc_entry*)((uintptr_t)(pml4_entry->address) << 12);
    struct paging_desc_entry* pdpt_entry = &pdpt_entries[pdpt_index];
    if (paging_null_entry(pdpt_entry))
    {
        void* new_pd = kzalloc(
            sizeof(struct paging_desc_entry) * PAGING_TOTAL_ENTRIES_PER_TABLE);
        pdpt_entry->address    = ((uintptr_t)new_pd) >> 12;
        pdpt_entry->present    = 1;
        pdpt_entry->read_write = 1;
    }

    struct paging_desc_entry* pd_entries =
        (struct paging_desc_entry*)((uintptr_t)(pdpt_entry->address) << 12);
    struct paging_desc_entry* pd_entry = &pd_entries[pd_index];
    if (paging_null_entry(pd_entry))
    {
        void* new_pt = kzalloc(
            sizeof(struct paging_desc_entry) * PAGING_TOTAL_ENTRIES_PER_TABLE);
        pd_entry->address    = ((uintptr_t)new_pt) >> 12;
        pd_entry->present    = 1;
        pd_entry->read_write = 1;
    }

    struct paging_desc_entry* pt_entries =
        (struct paging_desc_entry*)((uintptr_t)(pd_entry->address) << 12);
    struct paging_desc_entry* pt_entry = &pt_entries[pt_index];

    // If this PT slot was previously mapped to something else, flush
    // the TLB so the next memory access reflects the new mapping.
    if (!paging_null_entry(pt_entry))
    {
        paging_invalidate_tlb_entry(virt);
    }

    pt_entry->address    = ((uintptr_t)phys) >> 12;
    pt_entry->present    = (flags & PAGING_IS_PRESENT)  ? 1 : 0;
    pt_entry->read_write = (flags & PAGING_IS_WRITEABLE) ? 1 : 0;

    return res;
}

// Lecture 21 - walk the BIOS E820 dump and identity-map every
// type-1 (usable) region into `desc`. The E820 buffer at 0x7E00
// survives kheap_init since L20 moved the bitmap to 16 MiB, so
// this can be called any time after kheap_init.
//
// Misaligned bases get aligned UP (round to next page); misaligned
// ends get aligned DOWN (round to current page). This intentionally
// excludes any sub-page leftover on either side - those would have
// been usable RAM but we can only map whole pages anyway.
int paging_map_e820_memory_regions(struct paging_desc* desc)
{
    // Lecture 23 - unconditionally identity-map the first 1 MiB.
    // Real-mode BIOS data, IVT remnants, VGA (0xB8000), boot
    // sector at 0x7C00, E820 dump at 0x7E00, kernel image at
    // 0x100000 - they all live here. Marking them present in
    // the new descriptor lets us paging_switch without
    // immediately faulting on a banner write.
    paging_map_to(desc, (void*)0x00, (void*)0x00, (void*)0x100000,
                  PAGING_IS_WRITEABLE | PAGING_IS_PRESENT);

    size_t total_entries = e820_total_entries();
    for (size_t i = 0; i < total_entries; i++)
    {
        struct e820_entry* entry = e820_entry(i);
        if (entry->type != 1)
        {
            continue;
        }

        void* base_addr = (void*)(uintptr_t)entry->base_addr;
        void* end_addr  = (void*)(uintptr_t)(entry->base_addr + entry->length);

        if (!paging_is_aligned(base_addr))
        {
            base_addr = paging_align_address(base_addr);
        }
        if (!paging_is_aligned(end_addr))
        {
            end_addr = paging_align_to_lower_page(end_addr);
        }

        paging_map_to(desc, base_addr, base_addr, end_addr,
                      PAGING_IS_WRITEABLE | PAGING_IS_PRESENT);
    }
    return 0;
}

int paging_map_range(struct paging_desc* desc, void* virt, void* phys, size_t count, int flags)
{
    int res = 0;
    for (size_t i = 0; i < count; i++)
    {
        res = paging_map(desc, virt, phys, flags);
        if (res < 0)
        {
            break;
        }
        virt = (char*)virt + PAGING_PAGE_SIZE;
        phys = (char*)phys + PAGING_PAGE_SIZE;
    }
    return res;
}

int paging_map_to(struct paging_desc* desc, void* virt, void* phys, void* phys_end, int flags)
{
    int res = 0;
    if ((uintptr_t)virt     % PAGING_PAGE_SIZE) { res = -EINVARG; goto out; }
    if ((uintptr_t)phys     % PAGING_PAGE_SIZE) { res = -EINVARG; goto out; }
    if ((uintptr_t)phys_end % PAGING_PAGE_SIZE) { res = -EINVARG; goto out; }
    if ((uintptr_t)phys_end < (uintptr_t)phys)  { res = -EINVARG; goto out; }

    uint64_t total_bytes = (char*)phys_end - (char*)phys;
    size_t   total_pages = total_bytes / PAGING_PAGE_SIZE;
    res = paging_map_range(desc, virt, phys, total_pages, flags);
out:
    return res;
}
