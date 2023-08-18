#ifndef SLAB_H
#define SLAB_H

// Small-object slabs for kmalloc.
//
// Seven size classes, 32 B to 2 KiB in powers of two. Each slab is
// one 4 KiB page cut into equal objects, so kmalloc(50) costs 64
// bytes instead of a whole heap block, and every object is aligned
// to its class size.
//
// Pages come in chunks (1 MiB, smaller if memory is fragmented)
// from the kernel heap, which sits on the page-frame allocator. The
// per-page bookkeeping lives OUTSIDE the page, in a descriptor array
// per chunk, so a page holds exactly 4096 / size objects and a free
// can be validated: the pointer must be inside a chunk, on an object
// boundary, and currently allocated.
//
// Empty pages go back to a free-page pool shared by all classes.
// Chunks are not returned to the heap.

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define SLAB_MIN_SIZE       32
#define SLAB_MAX_SIZE       2048
#define SLAB_TOTAL_CLASSES  7

struct multiheap;

struct slab_stats {
    size_t chunks;
    size_t pages_total;      // pages in all chunks
    size_t pages_in_use;     // pages assigned to a class
    size_t objects_in_use;
    size_t bad_frees;        // frees rejected as invalid
};

void   slab_init(struct multiheap* backing);
void*  slab_alloc(size_t size);
// True when `ptr` lies inside slab memory (valid object or not).
bool   slab_owns(void* ptr);
// 0, or -EINVARG for a pointer that is not a live object.
int    slab_free(void* ptr);
// Class size of the live object at `ptr`, 0 if it is not one.
size_t slab_object_size(void* ptr);
void   slab_get_stats(struct slab_stats* out);

#endif
