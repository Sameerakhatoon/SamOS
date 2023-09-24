// Lecture 13 - C-side 64-bit paging.
//
// The static page tables in kernel.asm cover the bring-up identity
// map. From kernel_main onward we use paging_desc_new() to build
// new 4-level descriptor trees at runtime (one per process later),
// paging_map* to install ranges in them, paging_switch() to load
// CR3 with the new top-level table.
//
// The kernel lives in the upper half. Page-table
// entries hold physical addresses and the tables themselves are
// reached through the direct map (PAGING_PHYS_TO_VIRT). The kernel
// descriptor owns PML4 slots 256-511 and every other descriptor
// points the same slots at the same PDPTs, so the kernel half is
// shared and never freed with a process. Kernel pages are
// supervisor-only; an intermediate entry gets U=1 only when a user
// page is mapped under it, and never in the upper half.

#include "paging.h"
#include "memory/heap/kheap.h"
#include "memory/heap/heap.h"
#include "memory/memory.h"
#include "kernel.h"     // L43 - for panic()
#include "boot/boot_info.h"
#include "status.h"

static struct paging_desc* current_paging_desc = 0;

// The kernel descriptor, whose upper half every new
// descriptor copies (paging_kernel_desc_new).
static struct paging_desc* paging_kernel_template = 0;

// linker.ld - kernel image sections (kernel virtual addresses).
extern char _kernel_text_start[];
extern char _kernel_text_end[];
extern char _kernel_rodata_start[];
extern char _kernel_rodata_end[];
extern char _kernel_data_start[];

// L31 - paging_null_entry is defined further down; forward-
// declare it so paging_get (which we hoisted to the top with
// the other L27..L29 helpers) can call it.
static bool paging_null_entry(struct paging_desc_entry* entry);

// The table an entry points at, through the direct map.
static struct paging_desc_entry* paging_entry_table(struct paging_desc_entry* entry){
    return (struct paging_desc_entry*)PAGING_PHYS_TO_VIRT(((uint64_t)entry->address) << 12);
}

static uint64_t paging_direct_to_phys(void* virt){
    return (uint64_t)(uintptr_t)virt - SAMOS_DIRECT_MAP_BASE;
}

// One zeroed 4 KiB table. kpage_zalloc: whole frames, never a slab
// object, so the table is page aligned.
static struct paging_desc_entry* paging_table_new(void){
    return kpage_zalloc(sizeof(struct paging_desc_entry) * PAGING_TOTAL_ENTRIES_PER_TABLE);
}

struct paging_pml_entries* paging_pml4_entries_new(void)
{
    return (struct paging_pml_entries*)paging_table_new();
}

// Lecture 43 - recursive teardown of a paging tree.
//
// table_entry is a pointer to the BASE of a level-N table (its
// 512 entries laid out as a contiguous array). The level
// parameter says what kind of entries those are:
//
//   level == 4   table_entry points at a PML4 (8-byte entries, each
//                  pointing at a PDPT)
//   level == 3   table_entry points at a PDPT (entries point at PD)
//   level == 2   table_entry points at a PD   (entries point at PT)
//   level == 1   table_entry points at a PT   (entries point at 4-KiB
//                  pages - those are the LEAVES; we do not free
//                  the page itself because it is data, not table
//                  bookkeeping)
//
// We recurse from level N down to level 2, freeing the intermediate
// tables on the way out. At level 1 we just kfree the PT table.
// The pages mapped BY the PT are user data - the caller's
// responsibility to free.
static void paging_desc_entry_free(struct paging_desc_entry* table_entry,
                                   paging_map_level_t level)
{
    // The book returned early here when the table's FIRST
    // entry was null, leaking any table whose slot 0 is unused (a PD
    // for 0x400000 uses slot 2). Callers already skip null entries.
    if(level == 0){
        panic("paging_desc_entry_free: level must be at least 1\n");
    }
    if(level > PAGING_MAP_LEVEL_4){
        panic("paging_desc_entry_free: level > 4 not supported\n");
    }

    if(level > 1){
        // Walk the 512 entries of this table; recurse into the
        // non-null ones at level-1.
        for(int i = 0; i < PAGING_TOTAL_ENTRIES_PER_TABLE; i++){
            struct paging_desc_entry* entry = &table_entry[i];
            if(!paging_null_entry(entry) && entry->address){
                paging_desc_entry_free(paging_entry_table(entry), level - 1);
            }
        }
    }

    // Free THIS table's memory. (At level 1 we're freeing a PT
    // table; the level-1 walk above doesn't run, so we go straight
    // here.)
    kfree(table_entry);
}

// Lecture 43 - free an entire paging_desc tree.
//
// Only the lower half. PML4 slots 256-511 point at the
// kernel's shared PDPTs.
void paging_desc_free(struct paging_desc* desc){
    paging_map_level_t level = desc->level;
    for(int i = 0; i < PAGING_KERNEL_PML4_FIRST; i++){
        struct paging_desc_entry* entry = &desc->pml->entries[i];
        if(!paging_null_entry(entry) && entry->address){
            paging_desc_entry_free(paging_entry_table(entry), level - 1);
        }
    }
    kfree(desc->pml);
    kfree(desc);
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

// Lecture 31 - real implementation. Walks PML4 -> PDPT -> PD ->
// PT for `virt` and returns a pointer to the PT leaf entry, or
// NULL if any intermediate entry is missing. The returned PT
// entry may itself be null (not-present mapping); the caller
// (paging_get_physical_address) does that check.
//
// SamOs deviation: upstream has cut-paste bugs in the
// intermediate null checks (testing the entries-array pointer
// against a zero struct instead of the relevant entry). We write
// the clean walk.
struct paging_desc_entry* paging_get(struct paging_desc* desc, void* virt){
    uint64_t va = (uint64_t)virt;
    size_t idx[3] = { (va >> 39) & 0x1FF, (va >> 30) & 0x1FF, (va >> 21) & 0x1FF };
    struct paging_desc_entry* table = desc->pml->entries;
    for(int level = 0; level < 3; level++){
        struct paging_desc_entry* entry = &table[idx[level]];
        if(!entry->present){
            return NULL;
        }
        table = paging_entry_table(entry);
    }
    return &table[(va >> 12) & 0x1FF];
}

// Lecture 29 - virtual-to-physical helper. Uses paging_get to
// find the leaf entry, reconstructs the physical address as
// (entry.address << 12) + (virtual_address & 0xFFF). Returns
// NULL when the walk fails.
//
// Also NULL for a leaf that is not present (an unmapped
// page used to come back as its stale frame).
void* paging_get_physical_address(struct paging_desc* desc, void* virtual_address){
    struct paging_desc_entry* entry = paging_get(desc, virtual_address);
    if(!entry || !entry->present){
        return NULL;
    }
    uint64_t physical_base = ((uint64_t)entry->address) << 12;
    uint64_t offset        = ((uint64_t)virtual_address) & 0xFFF;
    return (void*)(physical_base + offset);
}

void paging_switch(struct paging_desc* desc)
{
    current_paging_desc = desc;
    paging_load_directory((uintptr_t*)paging_direct_to_phys(desc->pml));
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
    if (!desc->pml)
    {
        kfree(desc);
        return NULL;
    }
    desc->level = root_map_level;

    // Share the kernel half.
    if (paging_kernel_template)
    {
        for (int i = PAGING_KERNEL_PML4_FIRST; i < PAGING_TOTAL_ENTRIES_PER_TABLE; i++)
        {
            desc->pml->entries[i] = paging_kernel_template->pml->entries[i];
        }
    }
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

static bool paging_is_canonical(uintptr_t va)
{
    return va < SAMOS_USER_SPACE_END || va >= SAMOS_DIRECT_MAP_BASE;
}

// Make sure `entry` points at a table, allocating it on demand.
// The book set U=1 on every intermediate entry ("Lecture 59
// hot-fix"), so a later user mapping anywhere could reach through a
// table the kernel had built. Now U=1 is added only on the way to a
// user page, which paging_map allows only in the lower half.
static int paging_intermediate(struct paging_desc_entry* entry, bool user)
{
    if (paging_null_entry(entry))
    {
        struct paging_desc_entry* table = paging_table_new();
        if (!table)
        {
            return -ENOMEM;   // The book mapped page 0 here
        }
        entry->address    = paging_direct_to_phys(table) >> 12;
        entry->present    = 1;
        entry->read_write = 1;
    }
    if (user)
    {
        entry->user_supervisor = 1;
    }
    return 0;
}

// Install one virt -> phys 4-KiB mapping. Allocates missing
// intermediate tables (PDPT, PD, PT) on demand from kheap.
// `phys` is a PHYSICAL address. flags 0 unmaps.
int paging_map(struct paging_desc* desc, void* virt, void* phys, int flags)
{
    uintptr_t va = (uintptr_t)virt;
    bool user = (flags & PAGING_ACCESS_FROM_ALL) != 0;

    if (!paging_is_canonical(va) || (user && va >= SAMOS_USER_SPACE_END))
    {
        return -EINVARG;
    }

    // 48-bit canonical virtual address layout:
    //   bits 47..39 PML4 index
    //   bits 38..30 PDPT index
    //   bits 29..21 PD index
    //   bits 20..12 PT index
    //   bits 11..0  offset within the 4-KiB page
    size_t idx[3] = { (va >> 39) & 0x1FF, (va >> 30) & 0x1FF, (va >> 21) & 0x1FF };
    struct paging_desc_entry* table = desc->pml->entries;
    for (int level = 0; level < 3; level++)
    {
        struct paging_desc_entry* entry = &table[idx[level]];
        int res = paging_intermediate(entry, user);
        if (res < 0)
        {
            return res;
        }
        table = paging_entry_table(entry);
    }
    struct paging_desc_entry* pt_entry = &table[(va >> 12) & 0x1FF];
    bool was_mapped = !paging_null_entry(pt_entry);

    struct paging_desc_entry leaf = { 0 };
    if (flags)
    {
        leaf.address         = ((uintptr_t)phys) >> 12;
        leaf.present         = (flags & PAGING_IS_PRESENT)     ? 1 : 0;
        leaf.read_write      = (flags & PAGING_IS_WRITEABLE)   ? 1 : 0;
        leaf.user_supervisor = user ? 1 : 0;
        leaf.pwt             = (flags & PAGING_WRITE_THROUGH)  ? 1 : 0;
        leaf.pcd             = (flags & PAGING_CACHE_DISABLED) ? 1 : 0;
        leaf.execute_disable = (flags & PAGING_NO_EXECUTE)     ? 1 : 0;
    }
    *pt_entry = leaf;

    // If this PT slot was previously mapped to something else, flush
    // the TLB so the next memory access reflects the new mapping.
    if (was_mapped)
    {
        paging_invalidate_tlb_entry(virt);
    }
    return 0;
}

// Lecture 21 - walk the E820 dump and map every usable region.
//
// Into the kernel's direct map, supervisor-only and NX,
// instead of identity-mapping it user-writable into every process.
// The first MiB stays mapped (boot markers at 0x6000), and so do the
// loader's kernel window and boot data area (SAMOS_E820_TYPE_KERNEL).
//
// Misaligned bases get aligned UP (round to next page); misaligned
// ends get aligned DOWN (round to current page).
int paging_map_e820_memory_regions(struct paging_desc* desc)
{
    const int flags = PAGING_IS_PRESENT | PAGING_IS_WRITEABLE | PAGING_NO_EXECUTE;
    int res = paging_map_to(desc, PAGING_PHYS_TO_VIRT(0), (void*)0x00, (void*)0x100000, flags);
    if (res < 0)
    {
        return res;
    }

    size_t total_entries = e820_total_entries();
    for (size_t i = 0; i < total_entries; i++)
    {
        struct e820_entry* entry = e820_entry(i);
        // The loader reports the kernel window and boot data
        // area as their own type; they must stay mapped.
        if (entry->type != SAMOS_E820_TYPE_USABLE && entry->type != SAMOS_E820_TYPE_KERNEL)
        {
            continue;
        }

        void* base_addr = paging_align_address((void*)(uintptr_t)entry->base_addr);
        void* end_addr  = paging_align_to_lower_page((void*)(uintptr_t)(entry->base_addr + entry->length));
        if (end_addr <= base_addr)
        {
            continue;
        }
        res = paging_map_to(desc, PAGING_PHYS_TO_VIRT(base_addr), base_addr, end_addr, flags);
        if (res < 0)
        {
            return res;
        }
    }
    return 0;
}

// Map kernel image pages [start, end) to their physical
// addresses (virtual - SAMOS_KERNEL_VMA).
static int paging_map_kernel_image(struct paging_desc* desc, void* start, void* end, int flags)
{
    void* s = paging_align_to_lower_page(start);
    void* e = paging_align_address(end);
    return paging_map_to(desc, s, (void*)((uintptr_t)s - SAMOS_KERNEL_VMA),
                         (void*)((uintptr_t)e - SAMOS_KERNEL_VMA), flags);
}

// The kernel address space. Replaces kernel.asm's boot
// tables, which also identity-map the first GiB.
struct paging_desc* paging_kernel_desc_new(void)
{
    if (paging_kernel_template)
    {
        panic("paging_kernel_desc_new: called twice\n");
    }
    struct paging_desc* desc = paging_desc_new(PAGING_MAP_LEVEL_4);
    if (!desc)
    {
        return NULL;
    }

    // Every upper-half PML4 slot gets its PDPT now, so a process
    // descriptor made later sees all kernel mappings, present and
    // future, through its copied slots.
    for (int i = PAGING_KERNEL_PML4_FIRST; i < PAGING_TOTAL_ENTRIES_PER_TABLE; i++)
    {
        if (paging_intermediate(&desc->pml->entries[i], false) < 0)
        {
            return NULL;
        }
    }

    // W^X for the kernel itself: text RX, rodata R, data/.bss and the
    // boot stack (top of the window) RW.
    if (paging_map_e820_memory_regions(desc) < 0 ||
        paging_map_kernel_image(desc, _kernel_text_start, _kernel_text_end,
                                PAGING_IS_PRESENT) < 0 ||
        paging_map_kernel_image(desc, _kernel_rodata_start, _kernel_rodata_end,
                                PAGING_IS_PRESENT | PAGING_NO_EXECUTE) < 0 ||
        paging_map_kernel_image(desc, _kernel_data_start,
                                (void*)(SAMOS_KERNEL_VMA + SAMOS_KERNEL_WINDOW_END),
                                PAGING_IS_PRESENT | PAGING_IS_WRITEABLE | PAGING_NO_EXECUTE) < 0)
    {
        return NULL;
    }

    paging_kernel_template = desc;
    return desc;
}

uint64_t paging_kernel_virt_to_phys(void* virt)
{
    uintptr_t va = (uintptr_t)virt;
    if (PAGING_IS_DIRECT_MAP(va))
    {
        return va - SAMOS_DIRECT_MAP_BASE;
    }
    if (va >= SAMOS_KERNEL_VMA)
    {
        return va - SAMOS_KERNEL_VMA;
    }
    if (!paging_kernel_template)
    {
        return 0;
    }
    return (uint64_t)(uintptr_t)paging_get_physical_address(paging_kernel_template, virt);
}

void* paging_map_mmio(uint64_t phys, uint64_t size, int flags)
{
    if (!paging_kernel_template || size == 0)
    {
        return NULL;
    }
    uint64_t start = phys & ~(uint64_t)(PAGING_PAGE_SIZE - 1);
    uint64_t end   = paging_align_value_to_upper_page(phys + size);
    flags |= PAGING_IS_PRESENT | PAGING_IS_WRITEABLE | PAGING_NO_EXECUTE;
    if (paging_map_to(paging_kernel_template, PAGING_PHYS_TO_VIRT(start), (void*)start,
                      (void*)end, flags) < 0)
    {
        return NULL;
    }
    return PAGING_PHYS_TO_VIRT(phys);
}

int paging_user_translate(struct paging_desc* desc, uintptr_t virt, bool write, uint64_t* phys_out)
{
    if (virt >= SAMOS_USER_SPACE_END)
    {
        return -EFAULT;
    }
    size_t idx[4] = { (virt >> 39) & 0x1FF, (virt >> 30) & 0x1FF,
                      (virt >> 21) & 0x1FF, (virt >> 12) & 0x1FF };
    struct paging_desc_entry* table = desc->pml->entries;
    struct paging_desc_entry* entry = NULL;
    for (int level = 0; level < 4; level++)
    {
        entry = &table[idx[level]];
        if (!entry->present || !entry->user_supervisor || (write && !entry->read_write))
        {
            return -EFAULT;
        }
        if (level < 3)
        {
            table = paging_entry_table(entry);
        }
    }
    *phys_out = (((uint64_t)entry->address) << 12) | (virt & 0xFFF);
    return 0;
}

// Lecture 166 - page-up alignment helper. Pixel-buffer sizes
// rarely land on a page boundary, so the framebuffer mapper
// rounds up before calling paging_map_range.
uint64_t paging_align_value_to_upper_page(uint64_t val_in){
    if((uint64_t)val_in % PAGING_PAGE_SIZE){
        return ((uint64_t)val_in + PAGING_PAGE_SIZE - ((uint64_t)val_in % PAGING_PAGE_SIZE));
    }
    return val_in;
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
