// Physical page-frame allocator. See pfa.h.

#include "pfa.h"
#include "memory.h"
#include "config.h"
#include "kernel.h"
#include "status.h"
#include "memory/heap/heap.h"
#include "string/string.h"
#include "arch/arch.h"
#include "memory/heap/kheap.h"

// The frame table and every frame it hands out are direct-map
// addresses; the reserved ranges below stay physical.
#define PFA_VIRT(p) ((uintptr_t)PAGING_PHYS_TO_VIRT(p))

// End of the kernel image including .bss (linker.ld).
extern char samos_kernel_end[];

struct pfa_range {
    uintptr_t   start;
    uintptr_t   end;
};

#define PFA_MAX_RESERVED 8

static struct heap       pfa_frame_heap;
static struct heap_table pfa_frame_table;
static struct pfa_range  pfa_reserved[PFA_MAX_RESERVED];
static size_t            pfa_reserved_total = 0;

static uintptr_t pfa_align_up(uintptr_t v){
    return (v + PFA_FRAME_SIZE - 1) & ~(uintptr_t)(PFA_FRAME_SIZE - 1);
}

static uintptr_t pfa_align_down(uintptr_t v){
    return v & ~(uintptr_t)(PFA_FRAME_SIZE - 1);
}

static void pfa_add_reserved(uintptr_t start, uintptr_t end){
    if(pfa_reserved_total >= PFA_MAX_RESERVED){
        panic("pfa: too many reserved ranges\n");
    }
    pfa_reserved[pfa_reserved_total].start = pfa_align_down(start);
    pfa_reserved[pfa_reserved_total].end   = pfa_align_up(end);
    pfa_reserved_total++;
}

static bool pfa_overlaps(uintptr_t a0, uintptr_t a1, uintptr_t b0, uintptr_t b1){
    return a0 < b1 && b0 < a1;
}

// Lowest frame-aligned address, inside one usable region and below
// the boot identity map, where `bytes` fit without touching a
// reserved range. 0 when there is none.
static uintptr_t pfa_find_table_home(size_t bytes){
    uintptr_t best = 0;
    size_t total = e820_total_entries();
    for(size_t i = 0; i < total; i++){
        struct e820_entry* r = e820_entry(i);
        if(r->type != 1){
            continue;
        }
        uintptr_t s = r->base_addr < PFA_LOW_LIMIT ? PFA_LOW_LIMIT : r->base_addr;
        uintptr_t e = r->base_addr + r->length;
        if(e > PFA_EARLY_MAP_LIMIT){
            e = PFA_EARLY_MAP_LIMIT;
        }
        s = pfa_align_up(s);
        e = pfa_align_down(e);

        uintptr_t cand = s;
        while(cand + bytes <= e){
            bool moved = false;
            for(size_t j = 0; j < pfa_reserved_total; j++){
                if(pfa_overlaps(cand, cand + bytes, pfa_reserved[j].start, pfa_reserved[j].end)){
                    cand  = pfa_reserved[j].end;
                    moved = true;
                }
            }
            if(!moved){
                if(best == 0 || cand < best){
                    best = cand;
                }
                break;
            }
        }
    }
    return best;
}

void pfa_init(void){
    size_t total = e820_total_entries();
    if(total == 0){
        panic("pfa: the firmware memory map is empty\n");
    }

    // Loader and boot-services memory joins the usable RAM.
    print("pfa: reclaimed ");
    print(itoa((int)(e820_reclaim_loader_memory() / 1024)));
    print(" KiB of loader memory\n");

    // Span of usable RAM above the low limit.
    uintptr_t lo = UINTPTR_MAX;
    uintptr_t hi = 0;
    for(size_t i = 0; i < total; i++){
        struct e820_entry* r = e820_entry(i);
        if(r->type != 1){
            continue;
        }
        uintptr_t s = r->base_addr < PFA_LOW_LIMIT ? PFA_LOW_LIMIT : r->base_addr;
        uintptr_t e = r->base_addr + r->length;
        if(e <= s){
            continue;
        }
        if(s < lo) lo = s;
        if(e > hi) hi = e;
    }
    lo = pfa_align_up(lo);
    hi = pfa_align_down(hi);
    if(lo == UINTPTR_MAX || hi <= lo){
        panic("pfa: no usable RAM above 1 MiB\n");
    }
    size_t frames = (hi - lo) / PFA_FRAME_SIZE;

    // Memory in use at kernel entry. Since the UEFI boot work the loader reports
    // both as SAMOS_E820_TYPE_KERNEL (0x1001), so they are not in
    // the usable set above; the reservations stay as a second line
    // of defence against a loader that reports them as type 1:
    //   - the kernel image (.text through .bss) loaded at 1 MiB, and
    //     the boot stack kernel.asm points rsp at, growing down from
    //     SAMOS_BOOT_STACK_TOP towards the image;
    //   - the E820-format map the loader wrote at 0x210000.
    uintptr_t image_end = (uintptr_t)samos_kernel_end - SAMOS_KERNEL_VMA;
    if(image_end < SAMOS_BOOT_STACK_TOP){
        image_end = SAMOS_BOOT_STACK_TOP;
    }
    pfa_add_reserved(SAMOS_KERNEL_LOCATION, image_end);
    pfa_add_reserved(SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION,
                     SAMOS_MEMORY_MAP_LOCATION + total * sizeof(struct e820_entry));

    size_t table_bytes = pfa_align_up(frames * sizeof(HEAP_BLOCK_TABLE_ENTRY));
    uintptr_t home = pfa_find_table_home(table_bytes);
    if(!home){
        panic("pfa: no room below 1 GiB for the frame table\n");
    }
    pfa_add_reserved(home, home + table_bytes);

    pfa_frame_table.entries = (HEAP_BLOCK_TABLE_ENTRY*)PFA_VIRT(home);
    pfa_frame_table.total   = frames;
    if(heap_create(&pfa_frame_heap, (void*)PFA_VIRT(lo), (void*)PFA_VIRT(hi), &pfa_frame_table) < 0){
        panic("pfa: cannot create the frame table\n");
    }

    // Start with everything reserved, open up the usable regions,
    // then close the ranges in use again.
    heap_reserve_range(&pfa_frame_heap, PFA_VIRT(lo), PFA_VIRT(hi));
    for(size_t i = 0; i < total; i++){
        struct e820_entry* r = e820_entry(i);
        if(r->type == 1){
            heap_unreserve_range(&pfa_frame_heap, PFA_VIRT(r->base_addr), PFA_VIRT(r->base_addr + r->length));
        }
    }
    for(size_t i = 0; i < pfa_reserved_total; i++){
        heap_reserve_range(&pfa_frame_heap, PFA_VIRT(pfa_reserved[i].start), PFA_VIRT(pfa_reserved[i].end));
    }

    print("pfa: ");
    print(itoa((int)(pfa_free_frame_count() / 256)));
    print(" MiB free of ");
    print(itoa((int)(frames / 256)));
    print(" MiB span\n");
}

struct heap* pfa_heap(void){
    return &pfa_frame_heap;
}

// The frame table is also the multiheap's sub-heap, so it
// shares the kernel memory lock (kheap.c).
void* pfa_alloc_frames(size_t count){
    if(count == 0 || count > pfa_frame_table.total){
        return NULL;
    }
    kmem_lock();
    void* frames = heap_malloc(&pfa_frame_heap, count * PFA_FRAME_SIZE);
    kmem_unlock();
    return frames;
}

int pfa_free_frames(void* addr){
    kmem_lock();
    int res = heap_free(&pfa_frame_heap, addr);
    kmem_unlock();
    return res;
}

bool pfa_is_reserved(void* addr){
    if(!heap_is_address_within_heap(&pfa_frame_heap, addr)){
        return true;
    }
    return heap_is_block_reserved(&pfa_frame_heap, addr);
}

size_t pfa_total_frames(void){
    return pfa_frame_heap.total_blocks;
}

size_t pfa_free_frame_count(void){
    return pfa_frame_heap.free_blocks;
}

size_t pfa_used_frame_count(void){
    return pfa_frame_heap.used_blocks;
}

size_t pfa_reserved_frame_count(void){
    return pfa_frame_heap.reserved_blocks;
}
