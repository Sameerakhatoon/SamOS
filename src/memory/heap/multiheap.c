// Lecture 20 - multi-heap implementation.

#include "multiheap.h"
#include "kernel.h"
#include "status.h"

static struct multiheap_single_heap* multiheap_get_last_heap(struct multiheap* mh){
    struct multiheap_single_heap* cur = mh->first_multiheap;
    while(cur->next){
        cur = cur->next;
    }
    return cur;
}

struct multiheap* multiheap_new(struct heap* starting_heap){
    // Bootstrapping: the multiheap header lives inside the starting
    // heap. If this zalloc fails the kernel has no working heap at
    // all - the caller will panic.
    struct multiheap* mh = heap_zalloc(starting_heap, sizeof(struct multiheap));
    if(!mh){
        goto out;
    }
    mh->starting_heap   = starting_heap;
    mh->first_multiheap = 0;
    mh->total_heaps     = 0;
out:
    return mh;
}

static int multiheap_add_heap(struct multiheap* mh, struct heap* heap, int flags){
    struct multiheap_single_heap* node = heap_zalloc(mh->starting_heap, sizeof(struct multiheap_single_heap));
    if(!node){
        return -ENOMEM;
    }
    node->heap  = heap;
    node->flags = flags;
    node->next  = 0;

    if(!mh->first_multiheap){
        mh->first_multiheap = node;
    } else {
        multiheap_get_last_heap(mh)->next = node;
    }
    mh->total_heaps += 1;
    return 0;
}

int multiheap_add_existing_heap(struct multiheap* mh, struct heap* heap, int flags){
    flags |= MULTIHEAP_HEAP_FLAG_EXTERNALLY_OWNED;
    return multiheap_add_heap(mh, heap, flags);
}

int multiheap_add(struct multiheap* mh, void* saddr, void* eaddr, int flags){
    // L20 - dynamically build a new heap header + table for an
    // E820 region. Both live inside the starting heap.
    struct heap*       heap  = heap_zalloc(mh->starting_heap, sizeof(struct heap));
    struct heap_table* table = heap_zalloc(mh->starting_heap, sizeof(struct heap_table));
    if(!heap || !table){
        return -ENOMEM;
    }
    int res = heap_create(heap, saddr, eaddr, table);
    if(res < 0){
        heap_free(mh->starting_heap, heap);
        heap_free(mh->starting_heap, table);
        return res;
    }
    return multiheap_add_heap(mh, heap, flags);
}

void multiheap_free(struct multiheap* mh){
    struct multiheap_single_heap* cur = mh->first_multiheap;
    while(cur){
        struct multiheap_single_heap* next = cur->next;
        if(!(cur->flags & MULTIHEAP_HEAP_FLAG_EXTERNALLY_OWNED)){
            heap_free(mh->starting_heap, cur->heap);
        }
        cur = next;
    }
    heap_free(mh->starting_heap, mh);
}

static void* multiheap_alloc_first_pass(struct multiheap* mh, size_t size){
    void* ptr = NULL;
    struct multiheap_single_heap* cur = mh->first_multiheap;
    while(cur){
        ptr = heap_malloc(cur->heap, size);
        if(ptr){
            break;
        }
        cur = cur->next;
    }
    return ptr;
}

// L21+ stub. Will defragment by re-paging when fragmentation
// fails first-pass.
static void* multiheap_alloc_second_pass(struct multiheap* mh, size_t size){
    (void)mh;
    (void)size;
    return NULL;
}

void* multiheap_alloc(struct multiheap* mh, size_t size){
    // L20 - regular alloc does NOT defragment. Just first-pass.
    return multiheap_alloc_first_pass(mh, size);
}

void* multiheap_palloc(struct multiheap* mh, size_t size){
    // L20 - "paging-allowed" alloc tries first-pass, then will
    // fall through to second-pass defrag in later lectures.
    void* ptr = multiheap_alloc_first_pass(mh, size);
    if(ptr){
        return ptr;
    }
    return multiheap_alloc_second_pass(mh, size);
}
