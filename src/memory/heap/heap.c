#include "heap.h"
#include "kernel.h"
#include "status.h"
#include "memory/memory.h"
#include <stdbool.h>
#include <stdint.h>

// L23 - block-index types widened from int to int64_t. Forward
// declarations updated to match.
static int       heap_validate_table(void* ptr, void* end, struct heap_table* table);
static bool      heap_validate_alignment(void* ptr);
// L24 - un-staticed and exposed via heap.h.
uintptr_t heap_align_value_to_upper(uintptr_t val);
static int       heap_get_entry_type(HEAP_BLOCK_TABLE_ENTRY entry);
static int64_t   heap_get_start_block(struct heap* heap, uintptr_t total_blocks);
static void*     heap_block_to_address(struct heap* heap, int64_t block);
static void      heap_mark_blocks_taken(struct heap* heap, int64_t start_block, int64_t total_blocks);
static void      heap_mark_blocks_free(struct heap* heap, int64_t starting_block);
static int64_t   heap_address_to_block(struct heap* heap, void* address);
static void*     heap_malloc_blocks(struct heap* heap, uintptr_t total_blocks);

static int heap_validate_table(void* ptr, void* end, struct heap_table* table){
    int res = 0;
    size_t table_size   = (size_t)(end - ptr);
    size_t total_blocks = table_size / SAMOS_HEAP_BLOCK_SIZE;
    if(table->total != total_blocks){
        res = -EINVARG;
        goto out;
    }
out:
    return res;
}

static bool heap_validate_alignment(void* ptr){
    // L10 port fix: `unsigned int` is 4 bytes on x86_64; casting a
    // void* through it truncates the high 32 bits and breaks the
    // alignment check for any pointer above 4 GiB. Use uintptr_t
    // which is exactly the platform pointer width (8 bytes here).
    return ((uintptr_t)ptr % SAMOS_HEAP_BLOCK_SIZE) == 0;
}

int heap_create(struct heap* heap, void* ptr, void* end, struct heap_table* table){
    int res = 0;

    if(!heap_validate_alignment(ptr) || !heap_validate_alignment(end)){
        res = -EINVARG;
        goto out;
    }

    memset(heap, 0, sizeof(struct heap));
    heap->saddr = ptr;
    // L20 - remember the end of the data pool so multiheap can
    // route heap_free by address range.
    heap->eaddr = end;
    heap->table = table;

    res = heap_validate_table(ptr, end, table);
    if(res < 0){
        goto out;
    }

    size_t table_size = sizeof(HEAP_BLOCK_TABLE_ENTRY) * table->total;
    memset(table->entries, HEAP_BLOCK_TABLE_ENTRY_FREE, table_size);

out:
    return res;
}

uintptr_t heap_align_value_to_upper(uintptr_t val){
    if((val % SAMOS_HEAP_BLOCK_SIZE) == 0){
        return val;
    }
    val = (val - (val % SAMOS_HEAP_BLOCK_SIZE));
    val += SAMOS_HEAP_BLOCK_SIZE;
    return val;
}

// Lecture 24 - round DOWN to the previous block boundary.
// Symmetric counterpart to heap_align_value_to_upper. Used by
// the multiheap-defragment code in later lectures.
uintptr_t heap_align_value_to_lower(uintptr_t val){
    if((val % SAMOS_HEAP_BLOCK_SIZE) == 0){
        return val;
    }
    val = val - (val % SAMOS_HEAP_BLOCK_SIZE);
    return val;
}

static int heap_get_entry_type(HEAP_BLOCK_TABLE_ENTRY entry){
    return entry & 0x0F;
}

// Lecture 25 - inclusive range check. eaddr is the END boundary
// recorded by heap_create (one past the last byte), so `ptr ==
// eaddr` is a sentinel and treated as belonging to this heap.
bool heap_is_address_within_heap(struct heap* heap, void* ptr){
    return ptr >= heap->saddr && ptr <= heap->eaddr;
}

// Lecture 23 - promote heap-block-index types from int (32-bit)
// to int64_t so heaps larger than ~8 TiB at 4-KiB blocks don't
// overflow the index space. Also fix two latent bugs:
//   1) heap_get_start_block returned `bs` whenever bs != -1, but
//      bs could be set for a PARTIAL run at the end of the table
//      that never reached total_blocks. Check `bc != total_blocks`
//      instead.
//   2) heap_mark_blocks_taken set HAS_NEXT on every block where
//      `i != end_block - 1`, i.e. it dropped HAS_NEXT one block
//      too early. The last in-range block was marked terminal
//      but the SECOND-TO-LAST was missing HAS_NEXT too - so a
//      heap_free would stop one block short of the allocation.
//      Correct condition is `i != end_block` (only the very last
//      block lacks HAS_NEXT).
static int64_t heap_get_start_block(struct heap* heap, uintptr_t total_blocks){
    struct heap_table* table = heap->table;
    int64_t bc = 0;
    int64_t bs = -1;

    for(size_t i = 0; i < table->total; i++){
        if(heap_get_entry_type(table->entries[i]) != HEAP_BLOCK_TABLE_ENTRY_FREE){
            bc = 0;
            bs = -1;
            continue;
        }
        if(bs == -1){
            bs = i;
        }
        bc++;
        if(bc == (int64_t)total_blocks){
            break;
        }
    }

    // L23 fix: previously checked `bs == -1`, which missed the
    // case of a partial run at the end of the table.
    if(bc != (int64_t)total_blocks){
        return -ENOMEM;
    }
    return bs;
}

static void* heap_block_to_address(struct heap* heap, int64_t block){
    return heap->saddr + (block * SAMOS_HEAP_BLOCK_SIZE);
}

static void heap_mark_blocks_taken(struct heap* heap, int64_t start_block, int64_t total_blocks){
    int64_t end_block = (start_block + total_blocks) - 1;

    HEAP_BLOCK_TABLE_ENTRY entry = HEAP_BLOCK_TABLE_ENTRY_TAKEN | HEAP_BLOCK_IS_FIRST;
    if(total_blocks > 1){
        entry |= HEAP_BLOCK_HAS_NEXT;
    }

    for(int64_t i = start_block; i <= end_block; i++){
        heap->table->entries[i] = entry;
        entry = HEAP_BLOCK_TABLE_ENTRY_TAKEN;
        // L23 fix: was `i != end_block - 1`, off by one.
        if(i != end_block){
            entry |= HEAP_BLOCK_HAS_NEXT;
        }
    }
}

static void* heap_malloc_blocks(struct heap* heap, uintptr_t total_blocks){
    void* address = 0;

    int64_t start_block = heap_get_start_block(heap, total_blocks);
    if(start_block < 0){
        goto out;
    }

    address = heap_block_to_address(heap, start_block);

    heap_mark_blocks_taken(heap, start_block, total_blocks);

out:
    return address;
}

static void heap_mark_blocks_free(struct heap* heap, int64_t starting_block){
    struct heap_table* table = heap->table;
    for(int64_t i = starting_block; i < (int64_t)table->total; i++){
        HEAP_BLOCK_TABLE_ENTRY entry = table->entries[i];
        table->entries[i] = HEAP_BLOCK_TABLE_ENTRY_FREE;
        if(!(entry & HEAP_BLOCK_HAS_NEXT)){
            break;
        }
    }
}

static int64_t heap_address_to_block(struct heap* heap, void* address){
    return ((int64_t)(address - heap->saddr)) / SAMOS_HEAP_BLOCK_SIZE;
}

void* heap_malloc(struct heap* heap, size_t size){
    size_t  aligned_size = heap_align_value_to_upper(size);
    int64_t total_blocks = aligned_size / SAMOS_HEAP_BLOCK_SIZE;
    return heap_malloc_blocks(heap, total_blocks);
}

void heap_free(struct heap* heap, void* ptr){
    heap_mark_blocks_free(heap, heap_address_to_block(heap, ptr));
}

// Lecture 16 - bookkeeping helpers. Used by kheap_get callers to
// print usage stats (kernel_main) and, later, by the multi-heap
// orchestrator (L20+) to pick which sub-heap has room.

size_t heap_total_size(struct heap* heap){
    return heap->table->total * SAMOS_HEAP_BLOCK_SIZE;
}

size_t heap_total_used(struct heap* heap){
    size_t total = 0;
    struct heap_table* table = heap->table;
    for(size_t i = 0; i < table->total; i++){
        if(heap_get_entry_type(table->entries[i]) == HEAP_BLOCK_TABLE_ENTRY_TAKEN){
            total += SAMOS_HEAP_BLOCK_SIZE;
        }
    }
    return total;
}

size_t heap_total_available(struct heap* heap){
    return heap_total_size(heap) - heap_total_used(heap);
}

// Lecture 20 - per-heap zalloc. The kheap-level kzalloc currently
// (L20) does not work because multiheap_zalloc is not in yet;
// internal callers go through this until the kheap path catches
// up in a later lecture.
void* heap_zalloc(struct heap* heap, size_t size){
    void* ptr = heap_malloc(heap, size);
    if(!ptr){
        return 0;
    }
    memset(ptr, 0x00, size);
    return ptr;
}
