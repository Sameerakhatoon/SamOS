// Lecture 20 - kheap rewritten on top of the multiheap.
//
// Boot sequence:
//   1. Find the first E820 type-1 region big enough for the
//      minimal heap (SAMOS_HEAP_SIZE_BYTES).
//   2. Place the minimal heap's table at a fixed kernel address
//      (SAMOS_MINIMAL_HEAP_TABLE_ADDRESS = 16 MiB). The data
//      pool starts MINIMAL_HEAP_TABLE_SIZE bytes later.
//   3. Build the multiheap, register the minimal heap as the
//      "starting heap" (so multiheap node allocations land in it).
//   4. Walk the remaining E820 type-1 regions and register each
//      as an additional sub-heap.
//
// kmalloc -> multiheap_alloc (first-pass: try each sub-heap in
// order). kzalloc and kfree are STUBS at L20 (return NULL /
// no-op) because the multiheap-level versions are not built yet -
// upstream commits these as TODOs and patches them in later
// lectures. We do the same.

#include "kheap.h"
#include "heap.h"
#include "multiheap.h"
#include "config.h"
#include "kernel.h"
#include "memory/memory.h"

struct heap       kernel_minimal_heap;
struct heap_table kernel_minimal_heap_table;
struct multiheap* kernel_multiheap = NULL;

static struct e820_entry* kheap_get_allowable_region_for_minimal_heap(void){
    size_t total = e820_total_entries();
    for(size_t i = 0; i < total; i++){
        struct e820_entry* cur = e820_entry(i);
        if(cur->type == 1 && cur->length > SAMOS_HEAP_SIZE_BYTES){
            return cur;
        }
    }
    return NULL;
}

void kheap_init(void){
    struct e820_entry* entry = kheap_get_allowable_region_for_minimal_heap();
    if(!entry){
        panic("Installed RAM does not meet the requirements for multiheap\n");
    }

    // Clamp the heap-table address up to SAMOS_MINIMAL_HEAP_TABLE_ADDRESS
    // so the bitmap never lands on top of the kernel image (loaded
    // at 1 MiB) or on top of the E820 buffer at 0x7E00.
    void* address = (void*)(uintptr_t)entry->base_addr;
    void* heap_table_address = address;
    if(heap_table_address < (void*)SAMOS_MINIMAL_HEAP_TABLE_ADDRESS){
        heap_table_address = (void*)SAMOS_MINIMAL_HEAP_TABLE_ADDRESS;
    }

    void* heap_address     = (char*)heap_table_address + SAMOS_MINIMAL_HEAP_TABLE_SIZE;
    void* heap_end_address = (char*)heap_address + SAMOS_HEAP_SIZE_BYTES;
    size_t size = (size_t)((char*)heap_end_address - (char*)heap_address);
    size_t total_table_entries = size / SAMOS_HEAP_BLOCK_SIZE;

    kernel_minimal_heap_table.entries = (HEAP_BLOCK_TABLE_ENTRY*)heap_table_address;
    kernel_minimal_heap_table.total   = total_table_entries;

    int res = heap_create(&kernel_minimal_heap, heap_address, heap_end_address, &kernel_minimal_heap_table);
    if(res < 0){
        panic("Failed to initialize minimal heap\n");
    }

    // Bootstrap: the multiheap header + node allocs come from the
    // minimal heap itself, which has just been initialised.
    kernel_multiheap = multiheap_new(&kernel_minimal_heap);
    multiheap_add_existing_heap(kernel_multiheap, &kernel_minimal_heap,
                                MULTIHEAP_HEAP_FLAG_EXTERNALLY_OWNED);

    // Register every OTHER usable E820 region as an additional
    // sub-heap. These get full multiheap ownership (heap header +
    // table allocated dynamically inside the minimal heap).
    size_t total = e820_total_entries();
    for(size_t i = 0; i < total; i++){
        struct e820_entry* cur = e820_entry(i);
        if(cur == entry || cur->type != 1){
            continue;
        }
        multiheap_add(kernel_multiheap,
                      (void*)(uintptr_t)cur->base_addr,
                      (void*)(uintptr_t)(cur->base_addr + cur->length),
                      MULTIHEAP_HEAP_FLAG_DEFRAGMENT_WITH_PAGING);
    }
}

void* kmalloc(size_t size){
    void* ptr = multiheap_alloc(kernel_multiheap, size);
    if(!ptr){
        panic("Failed to allocate memory\n");
    }
    return ptr;
}

// L20 - stubbed pending later-lecture multiheap_zalloc.
void* kzalloc(size_t size){
    (void)size;
    return NULL;
}

// L20 - stubbed pending later-lecture multiheap_free.
void kfree(void* ptr){
    (void)ptr;
}
