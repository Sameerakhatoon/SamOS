// Lecture 20 - kheap rewritten on top of the multiheap.
//
// The kernel heap now sits on the page-frame allocator:
//
//   1. pfa_init builds the frame table from the firmware memory map
//      (memory/pfa.c). It replaces the book's minimal heap, which
//      needed one E820 region over 100 MiB and kept its bitmap at a
//      fixed 0x01000000 that nothing reserved.
//   2. The multiheap gets the frame table as its one sub-heap (and
//      its bootstrap heap), still flagged for the paging-defragment
//      second pass, so kpalloc keeps working when RAM is fragmented.
//   3. Requests up to SLAB_MAX_SIZE (2 KiB) go to the slab layer
//      (slab.c); bigger ones take whole frames from the multiheap.
//
// kfree is real: slab objects go back to their slab, everything
// else to multiheap_free. Pointers the allocator does not own, and
// double frees, are rejected and counted instead of corrupting the
// tables.

#include "kheap.h"
#include "heap.h"
#include "multiheap.h"
#include "slab.h"
#include "config.h"
#include "kernel.h"
#include "memory/memory.h"
#include "memory/pfa.h"
#include "memory/paging/paging.h"
#include "lib/spinlock/spinlock.h"

struct multiheap* kernel_multiheap = NULL;

// One recursive lock for the PFA, the multiheap, the
// slabs and the kernel half of the page tables. They are one lock
// because they are one structure: the multiheap's sub-heap is the
// PFA's frame table, and an arena allocation maps pages (paging_map
// takes this lock for kernel-half addresses) whose tables come from
// the heap again.
static struct spinlock kmem_spinlock = SPINLOCK_INIT("kmem", LOCK_LEVEL_KMEM, 1);

void kmem_lock(void){
    spin_lock(&kmem_spinlock);
}

void kmem_unlock(void){
    spin_unlock(&kmem_spinlock);
}
static size_t kheap_bad_free_count = 0;

void kheap_init(void){
    pfa_init();

    // Bootstrap: the multiheap header and node come out of the
    // frame table itself, which pfa_init has just built.
    kernel_multiheap = multiheap_new(pfa_heap());
    if(!kernel_multiheap){
        panic("kheap: cannot allocate the multiheap\n");
    }
    if(multiheap_add_existing_heap(kernel_multiheap, pfa_heap(),
                                   MULTIHEAP_HEAP_FLAG_DEFRAGMENT_WITH_PAGING) < 0){
        panic("kheap: cannot register the frame table\n");
    }
    slab_init(kernel_multiheap);
}

static void* kmalloc_locked(size_t size){
    // L23 - drop the panic-on-null. Callers that NEED non-null
    // can check, and the OOM-drain demo in kernel_main relies
    // on kmalloc returning NULL when the multiheap is exhausted.
    if(size == 0 || !kernel_multiheap){
        return NULL;
    }
    if(size <= SLAB_MAX_SIZE){
        void* ptr = slab_alloc(size);
        if(ptr){
            return ptr;
        }
        // No slab page to be had; a whole frame still works.
    }
    return multiheap_alloc(kernel_multiheap, size);
}

// Lecture 32 - exported so kernel_main can call it after
// paging_switch. Internally just multiheap_ready.
void kheap_post_paging(void){
    multiheap_ready(kernel_multiheap);
}

// Lecture 22 - kzalloc unstubbed. Goes through the same
// first-pass alloc as kmalloc and memsets to zero. (No fancy
// "find a heap that already has a pre-zeroed block" yet.)
void* kzalloc(size_t size){
    void* ptr = kmalloc(size);
    if(!ptr){
        return 0;
    }
    memset(ptr, 0x00, size);
    return ptr;
}

// Lecture 22 - "paging-allowed" allocators. Use the multiheap
// palloc path (first-pass, then second-pass paging-defragment).
// Small sizes come from the slabs like kmalloc.
static void* kpalloc_locked(size_t size){
    void* ptr = NULL;
    if(size > 0 && size <= SLAB_MAX_SIZE){
        ptr = slab_alloc(size);
    }
    if(!ptr){
        ptr = multiheap_palloc(kernel_multiheap, size);
    }
    if(!ptr){
        panic("Failed to allocate memory\n");
    }
    return ptr;
}

void* kpzalloc(size_t size){
    void* ptr = kpalloc(size);
    if(!ptr){
        return 0;
    }
    memset(ptr, 0x00, size);
    return ptr;
}

// Fault injection for memtest: after `skip` more successful
// calls, kpage_zalloc fails once. -1 (the default) is off.
static int kheap_page_fail_countdown = -1;

void kheap_fail_page_alloc_after(int skip){
    kheap_page_fail_countdown = skip;
}

static void* kpage_zalloc_locked(size_t size){
    if(size == 0 || !kernel_multiheap){
        return NULL;
    }
    if(kheap_page_fail_countdown >= 0 && kheap_page_fail_countdown-- == 0){
        return NULL;
    }
    void* ptr = multiheap_alloc(kernel_multiheap, size);
    if(ptr){
        memset(ptr, 0x00, size);
    }
    return ptr;
}

static void kheap_reject_free(void){
    kheap_bad_free_count++;
    if(kheap_bad_free_count <= 8){
        print("kfree: ignoring a pointer that is not a live allocation\n");
    }
}

// The book left kfree a no-op ("pending multiheap_free"),
// so every kernel free leaked. kfree(NULL) stays a no-op.
void kfree(void* ptr){
    if(!ptr || !kernel_multiheap){
        return;
    }
    kmem_lock();
    int res = slab_owns(ptr) ? slab_free(ptr) : multiheap_free(kernel_multiheap, ptr);
    if(res < 0){
        kheap_reject_free();
    }
    kmem_unlock();
}

// Lecture 80 - real krealloc. Drops the L78 stub.
// Routes through multiheap_realloc which finds the owning
// sub-heap and calls heap_realloc.
// A slab object stays put while the new size fits its
// class, and otherwise moves to a fresh allocation.
static void* krealloc_locked(void* old_ptr, size_t new_size){
    if(!old_ptr){
        return kmalloc(new_size);
    }
    if(new_size == 0){
        kfree(old_ptr);
        return NULL;
    }
    if(slab_owns(old_ptr)){
        size_t old_size = slab_object_size(old_ptr);
        if(old_size == 0){
            kheap_reject_free();
            return NULL;
        }
        if(new_size <= old_size){
            return old_ptr;
        }
        void* new_ptr = kmalloc(new_size);
        if(!new_ptr){
            return NULL;
        }
        memcpy(new_ptr, old_ptr, (int)old_size);
        slab_free(old_ptr);
        return new_ptr;
    }
    return multiheap_realloc(kernel_multiheap, old_ptr, new_size);
}

void kheap_get_stats(struct kheap_stats* out){
    kmem_lock();
    memset(out, 0, sizeof(*out));
    out->frames_total    = pfa_total_frames();
    out->frames_free     = pfa_free_frame_count();
    out->frames_used     = pfa_used_frame_count();
    out->frames_reserved = pfa_reserved_frame_count();
    struct slab_stats s;
    slab_get_stats(&s);
    out->slab_chunks         = s.chunks;
    out->slab_pages_in_use   = s.pages_in_use;
    out->slab_objects_in_use = s.objects_in_use;
    out->bad_frees           = kheap_bad_free_count;
    kmem_unlock();
}

void* kmalloc(size_t size){
    kmem_lock();
    void* ptr = kmalloc_locked(size);
    kmem_unlock();
    return ptr;
}

void* kpalloc(size_t size){
    kmem_lock();
    void* ptr = kpalloc_locked(size);
    kmem_unlock();
    return ptr;
}

void* kpage_zalloc(size_t size){
    kmem_lock();
    void* ptr = kpage_zalloc_locked(size);
    kmem_unlock();
    return ptr;
}

void* krealloc(void* old_ptr, size_t new_size){
    kmem_lock();
    void* ptr = krealloc_locked(old_ptr, new_size);
    kmem_unlock();
    return ptr;
}
