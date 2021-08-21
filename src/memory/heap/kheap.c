#include "kheap.h"
#include "heap.h"
#include "config.h"
#include "kernel.h"
#include "memory/memory.h"

struct heap       kernel_heap;
struct heap_table kernel_heap_table;

// Lecture 16 - kheap_init takes a runtime size. The bytes that
// back the heap pool still live at SAMOS_HEAP_ADDRESS (configured
// in config.h); the caller decides how much of that region is
// dedicated to this kheap. Future lectures grow this into a
// multi-heap with per-region sizes computed from E820.
void kheap_init(size_t size){
    size_t total_table_entries = size / SAMOS_HEAP_BLOCK_SIZE;
    kernel_heap_table.entries  = (HEAP_BLOCK_TABLE_ENTRY*)(SAMOS_HEAP_TABLE_ADDRESS);
    kernel_heap_table.total    = total_table_entries;

    void* end = (void*)(SAMOS_HEAP_ADDRESS + size);
    int   res = heap_create(&kernel_heap, (void*)(SAMOS_HEAP_ADDRESS), end, &kernel_heap_table);
    if(res < 0){
        // L16 - heap_create failure was a soft "print and keep
        // going" before. Now it's a hard panic - any code after
        // a failed kheap will fault on the first kmalloc.
        panic("Failed to create heap\n");
    }
}

// Lecture 16 - expose the kernel heap for accounting and (later)
// for the multi-heap orchestrator to enumerate.
struct heap* kheap_get(void){
    return &kernel_heap;
}

void* kmalloc(size_t size){
    // L16 - panic on OOM instead of silently returning NULL.
    // Most early callers do not check the return value; a NULL
    // deref later is much harder to diagnose than a clear panic
    // here.
    void* ptr = heap_malloc(&kernel_heap, size);
    if(!ptr){
        panic("Failed to allocate memory\n");
    }
    return ptr;
}

void* kzalloc(size_t size){
    void* ptr = kmalloc(size);
    if(!ptr){
        return 0;
    }
    memset(ptr, 0x00, size);
    return ptr;
}

void kfree(void* ptr){
    heap_free(&kernel_heap, ptr);
}
