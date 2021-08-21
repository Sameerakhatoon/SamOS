#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>
#include <stddef.h>

struct heap;

void          kheap_init(size_t size);
void*         kmalloc(size_t size);
void*         kzalloc(size_t size);
void          kfree(void* ptr);
struct heap*  kheap_get(void);

#endif
