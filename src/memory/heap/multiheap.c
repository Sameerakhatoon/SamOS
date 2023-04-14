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

void multiheap_free(struct multiheap* mh){
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

// L21+ stub. Will defragment by re-paging when fragmentation
// fails first-pass.
static void* multiheap_alloc_second_pass(struct multiheap* mh, size_t size){
    (void)mh;
    (void)size;
    return NULL;
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
