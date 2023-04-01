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
void*         kzalloc(size_t size);  // L20 stubbed: returns NULL until multiheap_zalloc lands
void          kfree(void* ptr);      // L20 stubbed: no-op until multiheap_free lands

#endif
