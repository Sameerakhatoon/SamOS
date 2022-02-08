#ifndef KERNEL_VECTOR_H
#define KERNEL_VECTOR_H

#include <stddef.h>
#include <stdint.h>
#include "types.h"

// Lecture 78 - generic dynamic-array. Used by L78's disk
// restructure (vector of struct disk*) and later by process,
// IRQ, etc lists.

typedef int (*VECTOR_REORDER_FUNCTION)(void* first_element, void* second_element);

enum {
    VECTOR_NO_FLAGS = 0,
};

struct vector {
    void*  memory;
    int    flags;
    size_t e_size;               // bytes per element
    size_t t_elems;              // logical count
    size_t tm_elems;             // physical capacity (in elements)
    size_t t_reserved_elements;  // extra elements reserved per resize
};

struct vector* vector_new(size_t element_size,
                          size_t total_reserved_elements_per_resize,
                          int flags);
void           vector_free(struct vector* vec);
int            vector_push(struct vector* vec, void* elem);
int            vector_pop(struct vector* vec);
void           vector_reorder(struct vector* vec, VECTOR_REORDER_FUNCTION reorder);
int            vector_overwrite(struct vector* vec, int index, void* elem, size_t elem_size);
int            vector_back(struct vector* vec, void* data_out, size_t size);
int            vector_at(struct vector* vec, size_t index, void* data_out, size_t size);
size_t         vector_count(struct vector* vec);
int            vector_pop_element(struct vector* vec, void* mem_val, size_t size);

// Lecture 160 - grow the vector by total_elements live slots
// (zero-initialised). Unlike vector_push the values aren't
// written; index advances. Caller follows up with
// vector_overwrite.
int            vector_grow(struct vector* vec, size_t total_elements);

// Lecture 160 - linear scan for elem_val_ptr. Returns 0 on
// match (with *index_out set), -EOUTOFRANGE otherwise.
int            vector_has(struct vector* vec, void* elem_val_ptr,
                          size_t elem_size, size_t* index_out);

#endif
