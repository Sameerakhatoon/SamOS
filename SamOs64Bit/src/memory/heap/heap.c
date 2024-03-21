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
    heap->total_blocks    = table->total;
    heap->free_blocks     = table->total;
    heap->used_blocks     = 0;
    heap->reserved_blocks = 0;
    heap->search_hint     = 0;

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

// Lecture 25 - range check. eaddr is the END boundary recorded by
// heap_create (one past the last byte).
//
// The book treats `ptr == eaddr` as inside, but that address
// is the first byte of whatever follows the heap - for two adjacent
// sub-heaps it belongs to the next one. Half-open range instead.
bool heap_is_address_within_heap(struct heap* heap, void* ptr){
    return ptr >= heap->saddr && ptr < heap->eaddr;
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
//      Point 2 was wrong: see heap_mark_blocks_taken.
static int64_t heap_get_start_block(struct heap* heap, uintptr_t total_blocks){
    struct heap_table* table = heap->table;
    int64_t bc = 0;
    int64_t bs = -1;

    if(total_blocks == 0 || total_blocks > heap->free_blocks){
        return -ENOMEM;
    }

    // Start at the search hint; everything below it is taken.
    // And move the hint past the taken blocks the scan meets
    // before the first free one: a hint left on a reserved block made
    // every allocation rescan the whole table.
    bool seen_free = false;
    for(size_t i = heap->search_hint; i < table->total; i++){
        if(heap_get_entry_type(table->entries[i]) != HEAP_BLOCK_TABLE_ENTRY_FREE){
            bc = 0;
            bs = -1;
            if(!seen_free){
                heap->search_hint = i + 1;
            }
            continue;
        }
        seen_free = true;
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

// The L23 "fix" above was itself the bug. The old loop wrote
// entries[i] and then built the entry for block i+1, setting HAS_NEXT
// when `i != end_block` - which is true for every i that has a
// successor, so the LAST block got HAS_NEXT. heap_free then walked
// into the neighbouring allocation and released its first block. The
// 32-bit code (`i != end_block - 1` in that shape) was right. We now
// compute each block's own entry directly: HAS_NEXT on every block
// except the last, IS_FIRST on the first.
static void heap_mark_blocks_taken(struct heap* heap, int64_t start_block, int64_t total_blocks){
    int64_t end_block = (start_block + total_blocks) - 1;

    for(int64_t i = start_block; i <= end_block; i++){
        HEAP_BLOCK_TABLE_ENTRY entry = HEAP_BLOCK_TABLE_ENTRY_TAKEN;
        if(i == start_block){
            entry |= HEAP_BLOCK_IS_FIRST;
        }
        if(i != end_block){
            entry |= HEAP_BLOCK_HAS_NEXT;
        }
        heap->table->entries[i] = entry;

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
    if((size_t)start_block == heap->search_hint){
        heap->search_hint = start_block + total_blocks;
    }

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
    if((size_t)starting_block < heap->search_hint){
        heap->search_hint = starting_block;
    }
}

// L29 - exposed (was static). multiheap_free walks per-block
// addresses of virtual-arena allocations using this.
int64_t heap_address_to_block(struct heap* heap, void* address){
    return ((int64_t)(address - heap->saddr)) / SAMOS_HEAP_BLOCK_SIZE;
}

// Lecture 80 - inclusive "are blocks [start..end] all free?"
// check, used by heap_realloc's in-place grow path.
// A range that runs past the table is not free (the book
// read entries[] out of bounds here).
bool heap_is_block_range_free(struct heap* heap, size_t starting_block, size_t ending_block){
    struct heap_table* table = heap->table;
    if(ending_block < starting_block || ending_block >= table->total){
        return false;
    }
    for(size_t i = starting_block; i <= ending_block; i++){
        if(table->entries[i] & HEAP_BLOCK_TABLE_ENTRY_TAKEN){
            return false;
        }
    }
    return true;
}

// The only pointers heap_free / heap_realloc accept: inside
// the pool, block aligned, and the FIRST block of a live (not
// reserved) allocation.
bool heap_is_allocation_start(struct heap* heap, void* ptr){
    if(!heap_is_address_within_heap(heap, ptr) || !heap_validate_alignment(ptr)){
        return false;
    }
    HEAP_BLOCK_TABLE_ENTRY entry = heap->table->entries[heap_address_to_block(heap, ptr)];
    return (entry & HEAP_BLOCK_TABLE_ENTRY_TAKEN) &&
           (entry & HEAP_BLOCK_IS_FIRST) &&
           !(entry & HEAP_BLOCK_RESERVED);
}

bool heap_is_block_reserved(struct heap* heap, void* ptr){
    if(!heap_is_address_within_heap(heap, ptr)){
        return false;
    }
    return heap->table->entries[heap_address_to_block(heap, ptr)] & HEAP_BLOCK_RESERVED;
}

// Clamp [start, end) to the pool and convert to block indexes.
// `outward` picks reserve rounding (cover every touched block) over
// unreserve rounding (only whole blocks).
static bool heap_range_to_blocks(struct heap* heap, uintptr_t start, uintptr_t end, bool outward,
                                 size_t* first, size_t* last_excl){
    uintptr_t saddr = (uintptr_t)heap->saddr;
    uintptr_t eaddr = (uintptr_t)heap->eaddr;
    if(start < saddr) start = saddr;
    if(end > eaddr)   end   = eaddr;
    if(end <= start){
        return false;
    }
    if(outward){
        start = heap_align_value_to_lower(start - saddr);
        end   = heap_align_value_to_upper(end - saddr);
    } else {
        start = heap_align_value_to_upper(start - saddr);
        end   = heap_align_value_to_lower(end - saddr);
    }
    if(end <= start){
        return false;
    }
    *first     = start / SAMOS_HEAP_BLOCK_SIZE;
    *last_excl = end / SAMOS_HEAP_BLOCK_SIZE;
    return true;
}

void heap_reserve_range(struct heap* heap, uintptr_t start, uintptr_t end){
    size_t first, last;
    if(!heap_range_to_blocks(heap, start, end, true, &first, &last)){
        return;
    }
    for(size_t i = first; i < last; i++){
        if(heap->table->entries[i] != HEAP_BLOCK_TABLE_ENTRY_FREE){
            continue;
        }
        heap->table->entries[i] = HEAP_BLOCK_TABLE_ENTRY_TAKEN | HEAP_BLOCK_RESERVED;
        heap->free_blocks--;
        heap->reserved_blocks++;
    }
}

void heap_unreserve_range(struct heap* heap, uintptr_t start, uintptr_t end){
    size_t first, last;
    if(!heap_range_to_blocks(heap, start, end, false, &first, &last)){
        return;
    }
    for(size_t i = first; i < last; i++){
        if(!(heap->table->entries[i] & HEAP_BLOCK_RESERVED)){
            continue;
        }
        heap->table->entries[i] = HEAP_BLOCK_TABLE_ENTRY_FREE;
        heap->free_blocks++;
        heap->reserved_blocks--;
    }
    if(first < heap->search_hint){
        heap->search_hint = first;
    }
}

// Lecture 28 - count the TAKEN blocks starting at `starting_address`.
// Stops at the first block without HAS_NEXT (the terminator of
// the run). Returns 0 if `starting_address` is not the start of a
// live allocation (the book only checked for a negative
// block index).
size_t heap_allocation_block_count(struct heap* heap, void* starting_address){
    size_t count = 0;
    struct heap_table* table = heap->table;
    if(!heap_is_allocation_start(heap, starting_address)){
        goto out;
    }
    int64_t starting_block = heap_address_to_block(heap, starting_address);

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
    // heap_align_value_to_upper wraps to 0 for sizes within a
    // block of SIZE_MAX, so reject anything bigger than the pool
    // before rounding.
    if(size == 0 || size > heap_total_size(heap)){
        return NULL;
    }
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
//
// Fixes:
//   - `old_ptr` must be the start of a live allocation; anything
//     else returns NULL and changes nothing (the book trusted it).
//   - Shrink counted the freed blocks twice: heap_mark_blocks_free
//     already moves them from used to free, and the book then did
//     it again, so used_blocks drifted down and free_blocks up.
//   - Grow-in-place had no bounds check and could claim "free"
//     blocks past the end of the table.
void* heap_realloc(struct heap* heap, void* old_ptr, size_t new_size){
    if(!old_ptr){
        return heap_malloc(heap, new_size);
    }
    if(!heap_is_allocation_start(heap, old_ptr)){
        return NULL;
    }
    if(new_size == 0){
        heap_free(heap, old_ptr);
        return NULL;
    }
    if(new_size > heap_total_size(heap)){
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
        heap->table->entries[block_to_free - 1] &= ~HEAP_BLOCK_HAS_NEXT;
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
    void* new_addr = heap_malloc(heap, new_size_aligned);
    if(!new_addr){
        return NULL;
    }
    memcpy(new_addr, old_ptr, old_total_size);
    memset((char*)new_addr + old_total_size, 0, new_size_aligned - old_total_size);
    heap_free(heap, old_ptr);
    return new_addr;
}

// The book passed any pointer straight to the block walk, so
// a pointer into the middle of an allocation freed its tail, and a
// double free or a foreign pointer walked whatever the table held
// there. Now only the start of a live allocation is accepted.
int heap_free(struct heap* heap, void* ptr){
    if(!heap_is_allocation_start(heap, ptr)){
        return -EINVARG;
    }
    heap_mark_blocks_free(heap, heap_address_to_block(heap, ptr));
    return 0;
}

// Lecture 16 - bookkeeping helpers. Used by kheap_get callers to
// print usage stats (kernel_main) and, later, by the multi-heap
// orchestrator (L20+) to pick which sub-heap has room.
// Read from the running counts; reserved blocks are neither
// used nor available.

size_t heap_total_size(struct heap* heap){
    return heap->table->total * SAMOS_HEAP_BLOCK_SIZE;
}

size_t heap_total_used(struct heap* heap){
    return heap->used_blocks * SAMOS_HEAP_BLOCK_SIZE;
}

size_t heap_total_available(struct heap* heap){
    return heap->free_blocks * SAMOS_HEAP_BLOCK_SIZE;
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
