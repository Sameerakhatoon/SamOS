#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>
#include <stddef.h>

struct heap;

// Lecture 20 - kheap_init no longer takes a size.
// It builds the page-frame allocator from the firmware
// memory map, then the multiheap and the slab layer on top of it.
void          kheap_init(void);
void*         kmalloc(size_t size);
void*         kzalloc(size_t size);
void*         kpalloc(size_t size);   // L22 - palloc = first-pass, then paging-defragment second pass
void*         kpzalloc(size_t size);  // L22 - kpalloc + memset 0
// Whole zeroed frames, page aligned, identity address, never
// a slab object. For memory that gets mapped into a process: the
// mapping is per page, and a slab page is shared with other objects.
void*         kpage_zalloc(size_t size);
// Real. kfree(NULL) is a no-op; a pointer that is not a live
// allocation is ignored and counted in kheap_stats.bad_frees.
void          kfree(void* ptr);
// Lecture 80 - krealloc routes through multiheap_realloc.
void*         krealloc(void* old_ptr, size_t new_size);

// Lecture 32 - call once paging is set up. Hands the live
// kernel multiheap off to multiheap_ready: stand up the
// virtual arena, build shadow heaps for every paging-
// defragment sub-heap, lock the sub-heap set against further
// adds.
void          kheap_post_paging(void);

// Allocator counters, for the leak and stress self-tests.
struct kheap_stats {
    size_t frames_total;
    size_t frames_free;
    size_t frames_used;
    size_t frames_reserved;
    size_t slab_chunks;
    size_t slab_pages_in_use;
    size_t slab_objects_in_use;
    size_t bad_frees;
};
void          kheap_get_stats(struct kheap_stats* out);

#endif
