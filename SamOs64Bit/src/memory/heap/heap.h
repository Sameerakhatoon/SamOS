#ifndef HEAP_H
#define HEAP_H

#include "config.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define HEAP_BLOCK_TABLE_ENTRY_TAKEN    0x01
#define HEAP_BLOCK_TABLE_ENTRY_FREE     0x00

#define HEAP_BLOCK_HAS_NEXT             0b10000000
#define HEAP_BLOCK_IS_FIRST             0b01000000

typedef unsigned char HEAP_BLOCK_TABLE_ENTRY;

// Lecture 26 - per-heap callbacks fired on each block as a
// multi-block allocation is being marked taken or as a free walk
// reaches each block. The defragmenter (later lecture) hangs
// page-table operations off these: when a block in a paging-
// defragment heap is allocated, the callback installs the
// virtual-arena mapping for it; when it is freed, the callback
// tears the mapping down.
//
// Currently the size passed to the alloc callback is always
// SAMOS_HEAP_BLOCK_SIZE (one block) - the callback fires once
// per block within a multi-block allocation, not once per
// allocation.
typedef void* (*HEAP_BLOCK_ALLOCATED_CALLBACK_FUNCTION)(void* ptr, size_t size);
typedef void  (*HEAP_BLOCK_FREE_CALLBACK_FUNCTION)(void* ptr);

struct heap_table {
    HEAP_BLOCK_TABLE_ENTRY* entries;
    size_t total;
};

struct heap {
    struct heap_table* table;
    // Start address of the heap data pool.
    void* saddr;
    // L20 - end address of the heap data pool. Filled by
    // heap_create. Used by multiheap to test whether a freed
    // pointer belongs to a given sub-heap.
    void* eaddr;

    // Lecture 30 - running counts. heap_create initialises them
    // (free = total, used = 0). heap_malloc_blocks and
    // heap_mark_blocks_free keep them in sync. The defragment
    // second pass uses free_blocks to pick which physical sub-
    // heap can back a virtual allocation.
    size_t total_blocks;
    size_t free_blocks;
    size_t used_blocks;

    // Lecture 26 - per-block callbacks. NULL == no callback for
    // this heap (the default; the minimal heap and ordinary
    // E820-backed sub-heaps do not need either). Defragmenter
    // heaps will set both.
    HEAP_BLOCK_ALLOCATED_CALLBACK_FUNCTION  block_allocated_callback;
    HEAP_BLOCK_FREE_CALLBACK_FUNCTION       block_free_callback;
};

// Lecture 26 - convenience setter so callers do not poke struct
// fields directly.
void heap_callbacks_set(struct heap* heap,
                        HEAP_BLOCK_ALLOCATED_CALLBACK_FUNCTION allocated_func,
                        HEAP_BLOCK_FREE_CALLBACK_FUNCTION free_func);

int    heap_create(struct heap* heap, void* ptr, void* end, struct heap_table* table);
void*  heap_malloc(struct heap* heap, size_t size);
void*  heap_zalloc(struct heap* heap, size_t size);
void   heap_free(struct heap* heap, void* ptr);
size_t heap_total_size(struct heap* heap);
size_t heap_total_used(struct heap* heap);
size_t heap_total_available(struct heap* heap);

// Lecture 24 - block-size alignment helpers, exposed for the
// multiheap defragmenter and any future region sizing code.
uintptr_t heap_align_value_to_upper(uintptr_t val);
uintptr_t heap_align_value_to_lower(uintptr_t val);

// Lecture 25 - range check used by multiheap_get_heap_for_address
// to figure out which sub-heap owns a given pointer.
bool heap_is_address_within_heap(struct heap* heap, void* ptr);

// Lecture 80 - krealloc support.
bool  heap_is_block_range_free(struct heap* heap, size_t starting_block, size_t ending_block);
void* heap_realloc(struct heap* heap, void* old_ptr, size_t new_size);

// Lecture 28 - given a pointer to the start of an allocation,
// return the number of blocks the allocation spans. Used by
// the defragmenter to know how much virtual range to remap.
size_t heap_allocation_block_count(struct heap* heap, void* starting_address);

// Lecture 29 - exposed for multiheap_free which needs to walk
// the per-block addresses of a virtual-arena allocation.
int64_t heap_address_to_block(struct heap* heap, void* address);

#endif
