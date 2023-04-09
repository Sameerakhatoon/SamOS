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
    int          flags;
    struct multiheap_single_heap* next;
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
    // by a later lecture when the second-pass allocator lands.
    void*                          max_end_data_addr;

    size_t                         total_heaps;
};

struct multiheap* multiheap_new(struct heap* starting_heap);
int   multiheap_add_existing_heap(struct multiheap* mh, struct heap* heap, int flags);
int   multiheap_add(struct multiheap* mh, void* saddr, void* eaddr, int flags);
void* multiheap_alloc(struct multiheap* mh, size_t size);
void* multiheap_palloc(struct multiheap* mh, size_t size);
void  multiheap_free(struct multiheap* mh);

#endif
