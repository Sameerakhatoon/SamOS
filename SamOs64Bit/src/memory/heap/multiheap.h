#ifndef KERNEL_MULTIHEAP_H
#define KERNEL_MULTIHEAP_H

// Lecture 20 - multi-heap = a linked list of struct heap regions.
// kmalloc tries each in order until one returns non-null.
//
// The multiheap itself plus all per-region heap headers/tables it
// creates dynamically live inside `starting_heap` - a small heap
// the kernel built up front (the "minimal heap" in kheap.c). That
// makes multiheap bootstrappable: we cannot kmalloc a multiheap
// before there is a multiheap, so the first heap stays external.

#include "heap.h"

enum {
    // The heap header/table memory belongs to someone else (the
    // caller of multiheap_add_existing_heap). Don't heap_free it
    // when the multiheap is torn down.
    MULTIHEAP_HEAP_FLAG_EXTERNALLY_OWNED       = 0b00000001,

    // L21+ marker: heaps with this flag are candidates for a
    // future second-pass allocator that defragments by paging.
    // L20 stores it but does not act on it yet.
    MULTIHEAP_HEAP_FLAG_DEFRAGMENT_WITH_PAGING = 0b00000010,
};

struct multiheap_single_heap {
    struct heap* heap;
    // Lecture 27 - the "shadow" heap that mirrors this sub-heap
    // in the virtual arena. Lazily filled by multiheap_ready for
    // sub-heaps that carry MULTIHEAP_HEAP_FLAG_DEFRAGMENT_WITH_PAGING.
    // NULL otherwise.
    struct heap* paging_heap;
    int          flags;
    struct multiheap_single_heap* next;
};

// Lecture 27 - multiheap-level flags. IS_READY is set by
// multiheap_ready once the virtual arena is built; allocators
// gate the second-pass defragment path on it.
enum {
    MULTIHEAP_FLAG_IS_READY = 0x01,
};

struct multiheap {
    // Tiny "bootstrap" heap used to allocate multiheap_single_heap
    // nodes and any dynamic sub-heap headers/tables.
    struct heap*                   starting_heap;
    struct multiheap_single_heap*  first_multiheap;

    // Lecture 25 - end of the highest physical sub-heap. Pointers
    // above this address are interpreted as "virtual" - they map
    // to a defragment-by-paging arena that lives in the unused
    // virtual address space above the last sub-heap. Populated
    // by multiheap_ready (L27).
    void*                          max_end_data_addr;

    int                            flags;
    size_t                         total_heaps;
};

struct multiheap* multiheap_new(struct heap* starting_heap);
int   multiheap_add_existing_heap(struct multiheap* mh, struct heap* heap, int flags);
int   multiheap_add(struct multiheap* mh, void* saddr, void* eaddr, int flags);
void* multiheap_alloc(struct multiheap* mh, size_t size);
void* multiheap_palloc(struct multiheap* mh, size_t size);

// Lecture 29 - the OLD multiheap_free (tear down all sub-heaps)
// is now multiheap_free_heap; per-allocation free takes the
// short name to match kfree's expected wiring shape.
void  multiheap_free_heap(struct multiheap* mh);
void  multiheap_free(struct multiheap* mh, void* ptr);
// Lecture 80 - realloc through the multi-heap dispatch.
void* multiheap_realloc(struct multiheap* mh, void* old_ptr, size_t new_size);

// Lecture 27 - one-shot setup. Builds the virtual arena above
// max_end_data_addr; for every DEFRAGMENT_WITH_PAGING sub-heap,
// creates a shadow heap and identity-maps the virtual range
// into the live PML4. Caller must have a paging descriptor
// active (paging_switch already called).
int multiheap_ready(struct multiheap* mh);

// Lecture 28 - allocation introspection. Both walk the heap
// entry table from `ptr` and return the length of the
// contiguous TAKEN run (block_count) or its byte equivalent
// (byte_count).
size_t multiheap_allocation_block_count(struct multiheap* mh, void* ptr);
size_t multiheap_allocation_byte_count(struct multiheap* mh, void* ptr);

// Lecture 30 - state predicates.
bool multiheap_is_ready(struct multiheap* mh);
bool multiheap_can_add_heap(struct multiheap* mh);
bool multiheap_is_address_virtual(struct multiheap* mh, void* ptr);
struct multiheap_single_heap* multiheap_get_heap_for_address(struct multiheap* mh, void* address);

#endif
