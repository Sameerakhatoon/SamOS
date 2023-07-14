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
// L29 - un-staticed; declared in heap.h.
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

    // Lecture 30 - block accounting. Fresh heap is fully free.
    heap->total_blocks = table->total;
    heap->free_blocks  = table->total;
    heap->used_blocks  = 0;

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

// Lecture 26 - install per-block callbacks on a heap. Either
// argument may be NULL to keep that side unhooked.
void heap_callbacks_set(struct heap* heap,
                        HEAP_BLOCK_ALLOCATED_CALLBACK_FUNCTION allocated_func,
                        HEAP_BLOCK_FREE_CALLBACK_FUNCTION free_func){
    heap->block_allocated_callback = allocated_func;
    heap->block_free_callback      = free_func;
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

        // Lecture 26 - fire the per-block alloc callback if any.
        // Callbacks see one block at a time, even for multi-block
        // allocations. The "size" arg is always SAMOS_HEAP_BLOCK_SIZE.
        if(heap->block_allocated_callback){
            void* address = heap_block_to_address(heap, i);
            heap->block_allocated_callback(address, SAMOS_HEAP_BLOCK_SIZE);
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

    // Lecture 30 - bump the running counts.
    heap->used_blocks += total_blocks;
    heap->free_blocks -= total_blocks;

out:
    return address;
}

static void heap_mark_blocks_free(struct heap* heap, int64_t starting_block){
    struct heap_table* table = heap->table;
    size_t total_blocks_freed = 0;
    for(int64_t i = starting_block; i < (int64_t)table->total; i++){
        HEAP_BLOCK_TABLE_ENTRY entry = table->entries[i];
        table->entries[i] = HEAP_BLOCK_TABLE_ENTRY_FREE;

        // L30 - fire the free callback BEFORE the HAS_NEXT
        // terminator check so the last block in the chain gets
        // a callback too. (L26 placed the check first and
        // missed the terminator.)
        if(heap->block_free_callback){
            void* address = heap_block_to_address(heap, i);
            heap->block_free_callback(address);
        }

        total_blocks_freed++;
        if(!(entry & HEAP_BLOCK_HAS_NEXT)){
            break;
        }
    }
    // Lecture 30 - decrement used count by what we actually freed.
    heap->used_blocks -= total_blocks_freed;
    heap->free_blocks += total_blocks_freed;
}

// L29 - exposed (was static). multiheap_free walks per-block
// addresses of virtual-arena allocations using this.
int64_t heap_address_to_block(struct heap* heap, void* address){
    return ((int64_t)(address - heap->saddr)) / SAMOS_HEAP_BLOCK_SIZE;
}

// Lecture 80 - inclusive "are blocks [start..end] all free?"
// check, used by heap_realloc's in-place grow path.
bool heap_is_block_range_free(struct heap* heap, size_t starting_block, size_t ending_block){
    struct heap_table* table = heap->table;
    for(size_t i = starting_block; i <= ending_block; i++){
        if(table->entries[i] & HEAP_BLOCK_TABLE_ENTRY_TAKEN){
            return false;
        }
    }
    return true;
}

// Lecture 28 - count the TAKEN blocks starting at `starting_address`.
// Stops at the first block without HAS_NEXT (the terminator of
// the run). Returns 0 if `starting_address` is outside the heap
// or hits a free block immediately.
size_t heap_allocation_block_count(struct heap* heap, void* starting_address){
    size_t count = 0;
    struct heap_table* table = heap->table;
    int64_t starting_block = heap_address_to_block(heap, starting_address);
    if(starting_block < 0){
        goto out;
    }

    for(int64_t i = starting_block; i < (int64_t)table->total; i++){
        HEAP_BLOCK_TABLE_ENTRY entry = table->entries[i];
        if(entry & HEAP_BLOCK_TABLE_ENTRY_TAKEN){
            count++;
        }
        if(!(entry & HEAP_BLOCK_HAS_NEXT)){
            break;
        }
    }
out:
    return count;
}

void* heap_malloc(struct heap* heap, size_t size){
    size_t  aligned_size = heap_align_value_to_upper(size);
    int64_t total_blocks = aligned_size / SAMOS_HEAP_BLOCK_SIZE;
    return heap_malloc_blocks(heap, total_blocks);
}

// Lecture 80 - realloc.
//
// Three cases:
//   1. Shrink: free trailing blocks, clear HAS_NEXT on the new
//      last block.
//   2. Grow in place: if the blocks immediately after the
//      allocation are free, claim them.
//   3. Grow with move: zalloc a new region, memcpy old data
//      over, free the old region.
void* heap_realloc(struct heap* heap, void* old_ptr, size_t new_size){
    if(!old_ptr){
        return heap_malloc(heap, new_size);
    }
    if(new_size == 0){
        heap_free(heap, old_ptr);
        return NULL;
    }

    size_t  current_alloc_blocks = heap_allocation_block_count(heap, old_ptr);
    int64_t starting_block       = heap_address_to_block(heap, old_ptr);
    int64_t ending_block         = starting_block + current_alloc_blocks - 1;

    size_t new_size_aligned = heap_align_value_to_upper(new_size);
    size_t new_total_blocks = new_size_aligned / SAMOS_HEAP_BLOCK_SIZE;
    size_t old_total_size   = current_alloc_blocks * SAMOS_HEAP_BLOCK_SIZE;

    // Shrink.
    if(current_alloc_blocks >= new_total_blocks){
        if(current_alloc_blocks == new_total_blocks){
            return old_ptr;
        }
        int64_t block_to_free = starting_block + new_total_blocks;
        heap_mark_blocks_free(heap, block_to_free);

        if(new_total_blocks > 0){
            heap->table->entries[starting_block + new_total_blocks - 1] &= ~HEAP_BLOCK_HAS_NEXT;
        }
        size_t freed_blocks = current_alloc_blocks - new_total_blocks;
        heap->used_blocks -= freed_blocks;
        heap->free_blocks += freed_blocks;
        return old_ptr;
    }

    // Grow in place.
    size_t extra_blocks    = new_total_blocks - current_alloc_blocks;
    size_t extension_start = ending_block + 1;
    size_t extension_end   = extension_start + extra_blocks - 1;
    if(heap_is_block_range_free(heap, extension_start, extension_end)){
        for(size_t i = extension_start; i < extension_end; i++){
            heap->table->entries[i] = HEAP_BLOCK_TABLE_ENTRY_TAKEN | HEAP_BLOCK_HAS_NEXT;
        }
        heap->table->entries[extension_end] = HEAP_BLOCK_TABLE_ENTRY_TAKEN;
        heap->table->entries[ending_block]  |= HEAP_BLOCK_HAS_NEXT;
        heap->used_blocks += extra_blocks;
        heap->free_blocks -= extra_blocks;
        return old_ptr;
    }

    // Move.
    void* new_addr = heap_zalloc(heap, new_size_aligned);
    if(!new_addr){
        return NULL;
    }
    memcpy(new_addr, old_ptr, old_total_size);
    heap_free(heap, old_ptr);
    return new_addr;
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
