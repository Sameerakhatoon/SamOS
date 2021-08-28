#ifndef HEAP_H
#define HEAP_H

#include "config.h"
#include <stdint.h>
#include <stddef.h>

#define HEAP_BLOCK_TABLE_ENTRY_TAKEN    0x01
#define HEAP_BLOCK_TABLE_ENTRY_FREE     0x00

#define HEAP_BLOCK_HAS_NEXT             0b10000000
#define HEAP_BLOCK_IS_FIRST             0b01000000

typedef unsigned char HEAP_BLOCK_TABLE_ENTRY;

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
};

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

#endif
