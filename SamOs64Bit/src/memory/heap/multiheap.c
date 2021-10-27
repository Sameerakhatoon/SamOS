// Lecture 20 - multi-heap implementation.

#include "multiheap.h"
#include "kernel.h"
#include "memory/paging/paging.h"
#include "status.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

static struct multiheap_single_heap* multiheap_get_last_heap(struct multiheap* mh){
    struct multiheap_single_heap* cur = mh->first_multiheap;
    while(cur->next){
        cur = cur->next;
    }
    return cur;
}

struct multiheap* multiheap_new(struct heap* starting_heap){
    // Bootstrapping: the multiheap header lives inside the starting
    // heap. If this zalloc fails the kernel has no working heap at
    // all - the caller will panic.
    struct multiheap* mh = heap_zalloc(starting_heap, sizeof(struct multiheap));
    if(!mh){
        goto out;
    }
    mh->starting_heap   = starting_heap;
    mh->first_multiheap = 0;
    mh->total_heaps     = 0;
out:
    return mh;
}

// Lecture 25 - flag-check helper.
static bool multiheap_heap_allows_paging(struct multiheap_single_heap* heap){
    return heap->flags & MULTIHEAP_HEAP_FLAG_DEFRAGMENT_WITH_PAGING;
}

// Lecture 25 - return the highest physical eaddr across all
// sub-heaps. The second-pass defragmenter uses this as the base
// of the virtual arena (everything above is "virtual" memory).
void* multiheap_get_max_memory_end_address(struct multiheap* mh){
    void* max_addr = 0x00;
    struct multiheap_single_heap* cur = mh->first_multiheap;
    while(cur){
        if(cur->heap->eaddr >= max_addr){
            max_addr = cur->heap->eaddr;
        }
        cur = cur->next;
    }
    return max_addr;
}

// Lecture 25 - linear search by address range. O(N) over the
// number of sub-heaps. Once the multiheap routes free and zalloc
// by pointer, this is the hot path.
struct multiheap_single_heap* multiheap_get_heap_for_address(struct multiheap* mh, void* address){
    struct multiheap_single_heap* cur = mh->first_multiheap;
    while(cur){
        if(heap_is_address_within_heap(cur->heap, address)){
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

// Lecture 25 - "virtual" here is a multiheap concept, not a CPU
// one. The defragmenter exposes contiguous-looking allocations
// at addresses ABOVE max_end_data_addr by remapping the page
// tables. Anything in that range needs special handling on free.
bool multiheap_is_address_virtual(struct multiheap* mh, void* ptr){
    return ptr >= mh->max_end_data_addr;
}

// Lecture 30 - ready / can_add_heap predicates. Once
// multiheap_ready has fired, no more sub-heaps can be added
// (the shadow heaps + virtual arena are sized at ready time;
// inserting after would invalidate them).
bool multiheap_is_ready(struct multiheap* mh){
    return mh->flags & MULTIHEAP_FLAG_IS_READY;
}

bool multiheap_can_add_heap(struct multiheap* mh){
    return !multiheap_is_ready(mh);
}

// Lecture 25 - translate a virtual-arena pointer back to its
// physical owner by subtracting the virtual-arena base. The
// inverse mapping (phys -> virt) is built into the page tables
// the defragmenter installs.
void* multiheap_virtual_address_to_physical(struct multiheap* mh, void* ptr){
    return (void*)((uintptr_t)ptr - (uintptr_t)mh->max_end_data_addr);
}

// Lecture 28 - find the sub-heap whose SHADOW heap (paging_heap)
// contains the given virtual-arena pointer. O(N) over sub-heaps;
// only paging-defragment heaps are searched.
struct multiheap_single_heap* multiheap_get_paging_heap_for_address(struct multiheap* mh, void* address){
    struct multiheap_single_heap* cur = mh->first_multiheap;
    while(cur){
        if(!multiheap_heap_allows_paging(cur)){
            cur = cur->next;
            continue;
        }
        if(heap_is_address_within_heap(cur->paging_heap, address)){
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

// Lecture 28 - given a pointer that might be virtual-arena or
// physical, resolve to:
//   *heap_out         - the underlying physical sub-heap
//   *paging_heap_out  - the shadow heap if virtual, else NULL
//   *real_phys_addr   - the underlying physical address (== ptr
//                       if already physical)
void multiheap_get_heap_and_paging_heap_for_address(struct multiheap* mh,
                                                    void* ptr,
                                                    struct multiheap_single_heap** heap_out,
                                                    struct multiheap_single_heap** paging_heap_out,
                                                    void** real_phys_addr){
    void* real_addr = ptr;
    if(multiheap_is_address_virtual(mh, ptr)){
        *paging_heap_out = multiheap_get_paging_heap_for_address(mh, ptr);
        real_addr = multiheap_virtual_address_to_physical(mh, ptr);
    }
    *heap_out       = multiheap_get_heap_for_address(mh, real_addr);
    *real_phys_addr = real_addr;
}

// Lecture 28 - block count of the allocation that `ptr` heads.
// If `ptr` is in the virtual arena we walk the SHADOW heap (the
// run-length lives there, not in the physical sub-heap).
size_t multiheap_allocation_block_count(struct multiheap* mh, void* ptr){
    struct multiheap_single_heap* paging_heap = NULL;
    struct multiheap_single_heap* phys_heap   = NULL;
    void* real_phys_addr = NULL;
    multiheap_get_heap_and_paging_heap_for_address(mh, ptr, &phys_heap, &paging_heap, &real_phys_addr);

    // Prefer the shadow heap when present: the bitmap walk has
    // to use the SAME ptr we passed in, and that ptr is virtual.
    struct multiheap_single_heap* heap_to_check = paging_heap ? paging_heap : phys_heap;
    if(!heap_to_check){
        return 0;
    }
    return heap_allocation_block_count(heap_to_check->heap, ptr);
}

// Lecture 28 - byte count of the same allocation.
//
// SamOs deviation: upstream is recursive against itself
// (multiheap_allocation_byte_count calls
// multiheap_allocation_byte_count), which infinite-loops. The
// obvious intent is to call _block_count and multiply by block
// size. We do that.
size_t multiheap_allocation_byte_count(struct multiheap* mh, void* ptr){
    return multiheap_allocation_block_count(mh, ptr) * SAMOS_HEAP_BLOCK_SIZE;
}

// (The L25 helpers above are unused at L25 itself; later lectures
// in the multiheap arc call them. -Wno-unused-function in our
// CFLAGS keeps the static one quiet; the non-static ones do not
// warn even when uncalled.)

static int multiheap_add_heap(struct multiheap* mh, struct heap* heap, int flags){
    // Lecture 30 - refuse late additions. Ready time fixes the
    // sub-heap set; adding after would create a sub-heap without
    // a shadow and break the virtual-arena dispatch.
    if(!multiheap_can_add_heap(mh)){
        return -EINVARG;
    }
    struct multiheap_single_heap* node = heap_zalloc(mh->starting_heap, sizeof(struct multiheap_single_heap));
    if(!node){
        return -ENOMEM;
    }
    node->heap  = heap;
    node->flags = flags;
    node->next  = 0;

    if(!mh->first_multiheap){
        mh->first_multiheap = node;
    } else {
        multiheap_get_last_heap(mh)->next = node;
    }
    mh->total_heaps += 1;
    return 0;
}

int multiheap_add_existing_heap(struct multiheap* mh, struct heap* heap, int flags){
    flags |= MULTIHEAP_HEAP_FLAG_EXTERNALLY_OWNED;
    return multiheap_add_heap(mh, heap, flags);
}

int multiheap_add(struct multiheap* mh, void* saddr, void* eaddr, int flags){
    // L20 - dynamically build a new heap header + table for an
    // E820 region. Both live inside the starting heap.
    struct heap*       heap  = heap_zalloc(mh->starting_heap, sizeof(struct heap));
    struct heap_table* table = heap_zalloc(mh->starting_heap, sizeof(struct heap_table));
    if(!heap || !table){
        return -ENOMEM;
    }
    int res = heap_create(heap, saddr, eaddr, table);
    if(res < 0){
        heap_free(mh->starting_heap, heap);
        heap_free(mh->starting_heap, table);
        return res;
    }
    return multiheap_add_heap(mh, heap, flags);
}

// Lecture 29 - was the all-heaps teardown; renamed to
// _free_heap so the L29 per-allocation multiheap_free can take
// the well-known name.
void multiheap_free_heap(struct multiheap* mh){
    struct multiheap_single_heap* cur = mh->first_multiheap;
    while(cur){
        struct multiheap_single_heap* next = cur->next;
        if(!(cur->flags & MULTIHEAP_HEAP_FLAG_EXTERNALLY_OWNED)){
            heap_free(mh->starting_heap, cur->heap);
        }
        cur = next;
    }
    heap_free(mh->starting_heap, mh);
}

// Lecture 29 - per-allocation free.
//
// If `ptr` is in the virtual arena:
//   1. Walk every block in the allocation. For each block,
//      look up its physical backing via paging_get_physical_
//      address and recursively multiheap_free the physical
//      pointer (so the underlying physical sub-heap blocks
//      get released).
//   2. heap_free the virtual range on the shadow heap (so
//      the bookkeeping bitmap clears and the per-block free
//      callback fires to unmap the page).
//
// Else (`ptr` is physical):
//   1. heap_free on the owning physical sub-heap.
//
// Dormant at L29 - kheap.c::kfree is still a no-op, and the
// virtual-arena branch depends on the L31 paging_get
// implementation anyway. The path is wired here so L30 and
// later can finish the picture without further plumbing in
// multiheap.c.
void multiheap_free(struct multiheap* mh, void* ptr){
    struct multiheap_single_heap* paging_heap   = NULL;
    struct multiheap_single_heap* phys_heap     = NULL;
    void* real_phys_addr = NULL;
    multiheap_get_heap_and_paging_heap_for_address(mh, ptr, &phys_heap, &paging_heap, &real_phys_addr);

    if(paging_heap){
        size_t total_blocks   = heap_allocation_block_count(paging_heap->paging_heap, ptr);
        size_t starting_block = (size_t)heap_address_to_block(paging_heap->paging_heap, ptr);
        size_t ending_block   = starting_block + total_blocks;
        for(size_t i = starting_block; i < ending_block; i++){
            void* vaddr_for_block = (void*)((uintptr_t)ptr + (i * SAMOS_HEAP_BLOCK_SIZE));
            void* data_phys_addr  = paging_get_physical_address(paging_current_descriptor(),
                                                                 vaddr_for_block);
            // Recurse with the physical backing. The recursion
            // is bounded: the inner call's pointer is physical,
            // so it lands in the phys_heap branch and returns
            // without further recursion.
            multiheap_free(mh, data_phys_addr);
        }
        // Now release the virtual-arena range. The shadow heap's
        // free callback (multiheap_paging_heap_free_block) fires
        // per block and unmaps each PT entry.
        heap_free(paging_heap->paging_heap, ptr);
    } else if(phys_heap){
        heap_free(phys_heap->heap, real_phys_addr);
    }
}

static void* multiheap_alloc_first_pass(struct multiheap* mh, size_t size){
    void* ptr = NULL;
    struct multiheap_single_heap* cur = mh->first_multiheap;
    while(cur){
        ptr = heap_malloc(cur->heap, size);
        if(ptr){
            break;
        }
        cur = cur->next;
    }
    return ptr;
}

// Lecture 30 - first-pass paging alloc. Walk the sub-heaps that
// allow paging defragment; find one whose UNDERLYING PHYSICAL
// heap has enough free blocks; allocate a contiguous virtual
// range from its SHADOW heap. Returns the virtual address.
// `eligible_heap_out` is set to the chosen multiheap_single_heap
// so the second-pass loop can pull individual physical blocks
// from the SAME sub-heap.
static void* multiheap_alloc_paging(struct multiheap* mh, size_t size,
                                    struct multiheap_single_heap** eligible_heap_out){
    size_t total_required_blocks = size / SAMOS_HEAP_BLOCK_SIZE;
    struct multiheap_single_heap* cur = mh->first_multiheap;
    while(cur){
        if(!multiheap_heap_allows_paging(cur)){
            cur = cur->next;
            continue;
        }
        if(cur->heap->free_blocks < total_required_blocks){
            cur = cur->next;
            continue;
        }
        void* ptr = heap_malloc(cur->paging_heap, size);
        if(ptr){
            if(eligible_heap_out){
                *eligible_heap_out = cur;
            }
            return ptr;
        }
        cur = cur->next;
    }
    return NULL;
}

// Lecture 30 - second-pass defragment-by-paging allocator.
//
//   1. Round size up to a block boundary.
//   2. Ask multiheap_alloc_paging for a contiguous virtual range
//      from a sub-heap with enough free blocks.
//   3. For each block in the range, heap_zalloc one physical
//      block from the chosen physical sub-heap, and paging_map
//      the virtual address to it (present + writeable).
//
// Net effect: an allocation that asked for N contiguous blocks
// gets N possibly-scattered physical pages exposed as one
// contiguous virtual range.
static void* multiheap_alloc_second_pass(struct multiheap* mh, size_t size){
    struct paging_desc* desc = paging_current_descriptor();
    if(!desc){
        panic("multiheap second pass: paging not set up\n");
    }

    size = heap_align_value_to_upper(size);
    size_t total_blocks = size / SAMOS_HEAP_BLOCK_SIZE;
    struct multiheap_single_heap* chosen = NULL;

    void* vstart = multiheap_alloc_paging(mh, size, &chosen);
    if(!vstart){
        return NULL;
    }

    void* vcur = vstart;
    for(size_t i = 0; i < total_blocks; i++){
        void* phys = heap_zalloc(chosen->heap, SAMOS_HEAP_BLOCK_SIZE);
        if(!phys){
            // Paging heap said yes, physical said no - the two
            // bookkeeping pictures got out of sync. Hard bug.
            panic("multiheap second pass: paging heap promised more blocks than physical has\n");
        }
        paging_map(desc, vcur, phys,
                   PAGING_IS_WRITEABLE | PAGING_IS_PRESENT);
        vcur = (char*)vcur + SAMOS_HEAP_BLOCK_SIZE;
    }
    return vstart;
}

// Lecture 27 - free-callback installed on each shadow heap's
// block_free_callback by multiheap_ready. Tears the live page-
// table entry for the freed virtual page down (flags=0, present=
// 0) so a subsequent access faults. Reuses paging_map with a
// NULL phys + flags=0 as the unmap idiom.
void multiheap_paging_heap_free_block(void* ptr){
    paging_map(paging_current_descriptor(), ptr, NULL, 0);
}

// Lecture 27 - one-shot setup. For every DEFRAGMENT_WITH_PAGING
// sub-heap, build a shadow heap that lives in the virtual arena
// at (max_end + physical_addr_of_sub_heap), identity-map the
// shadow's range into the live PML4 (as not-present so first
// access faults), and hook the free callback.
//
// Caller MUST have a paging_desc loaded - we panic otherwise.
// Idempotent only in the sense that calling twice clobbers the
// existing shadow heaps (callers should not).
//
// At L27 nothing actually calls this; "Lecture 32 - calling
// multiheap_ready" is when the wiring goes live.
int multiheap_ready(struct multiheap* mh){
    int res = 0;
    mh->flags |= MULTIHEAP_FLAG_IS_READY;

    struct paging_desc* paging_desc = paging_current_descriptor();
    if(!paging_desc){
        panic("multiheap_ready: paging not set up\n");
    }

    void* max_end_addr = multiheap_get_max_memory_end_address(mh);
    mh->max_end_data_addr = max_end_addr;

    struct multiheap_single_heap* cur = mh->first_multiheap;
    while(cur){
        if(multiheap_heap_allows_paging(cur)){
            // Shadow range in the virtual arena. Lays the
            // physical sub-heap's address space directly atop
            // max_end_addr so virtual_to_physical is a simple
            // subtraction (matches L25's helper).
            void* vstart = (char*)max_end_addr + (uintptr_t)cur->heap->saddr;
            void* vend   = (char*)max_end_addr + (uintptr_t)cur->heap->eaddr;

            // Shadow bitmap + heap header come out of the
            // starting heap (the multiheap bootstrap arena).
            struct heap_table* sht = heap_zalloc(mh->starting_heap, sizeof(struct heap_table));
            sht->entries = heap_zalloc(mh->starting_heap,
                                       cur->heap->table->total * sizeof(HEAP_BLOCK_TABLE_ENTRY));
            sht->total   = cur->heap->table->total;

            // SamOs deviation: upstream calls a nonexistent
            // heap_kalloc here. Use heap_zalloc - same effect
            // (we want a zeroed struct heap anyway).
            struct heap* shadow = heap_zalloc(mh->starting_heap, sizeof(struct heap));
            heap_create(shadow, vstart, vend, sht);

            // Reserve the virtual range in the page tables but
            // mark not-present (flags=0). First access to a
            // shadow page faults; the alloc callback (later
            // lecture) will turn it on.
            paging_map_to(paging_desc, vstart, vstart, vend, 0);

            // Only the free side is hooked at L27 - it unmaps
            // the page on free.
            heap_callbacks_set(shadow, NULL, multiheap_paging_heap_free_block);
            cur->paging_heap = shadow;
        }
        cur = cur->next;
    }
    return res;
}

void* multiheap_alloc(struct multiheap* mh, size_t size){
    // L20 - regular alloc does NOT defragment. Just first-pass.
    return multiheap_alloc_first_pass(mh, size);
}

void* multiheap_palloc(struct multiheap* mh, size_t size){
    // L20 - "paging-allowed" alloc tries first-pass, then will
    // fall through to second-pass defrag in later lectures.
    void* ptr = multiheap_alloc_first_pass(mh, size);
    if(ptr){
        return ptr;
    }
    return multiheap_alloc_second_pass(mh, size);
}
