#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>
#include <stddef.h>

struct heap;

// Lecture 20 - kheap_init no longer takes a size. The size is
// SAMOS_HEAP_SIZE_BYTES for the minimal heap; additional E820
// regions are picked up automatically and their sizes come from
// the E820 entry itself.
void          kheap_init(void);
void*         kmalloc(size_t size);
void*         kzalloc(size_t size);
void*         kpalloc(size_t size);   // L22 - palloc = first-pass, then paging-defragment second pass
void*         kpzalloc(size_t size);  // L22 - kpalloc + memset 0
void          kfree(void* ptr);       // still stubbed: no-op until multiheap_free lands
// Lecture 80 - krealloc routes through multiheap_realloc.
void*         krealloc(void* old_ptr, size_t new_size);

// Lecture 32 - call once paging is set up. Hands the live
// kernel multiheap off to multiheap_ready: stand up the
// virtual arena, build shadow heaps for every paging-
// defragment sub-heap, lock the sub-heap set against further
// adds.
void          kheap_post_paging(void);

#endif
