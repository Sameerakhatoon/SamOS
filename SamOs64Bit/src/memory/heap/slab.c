// Small-object slabs for kmalloc. See slab.h.

#include "slab.h"
#include "multiheap.h"
#include "status.h"
#include "memory/memory.h"

#define SLAB_PAGE_SIZE          4096
#define SLAB_CHUNK_PAGES        256     // 1 MiB per chunk when possible
#define SLAB_MIN_CHUNK_PAGES    8
#define SLAB_MAX_CHUNKS         256
#define SLAB_NO_CLASS           0xFFFF
#define SLAB_MAX_PER_PAGE       (SLAB_PAGE_SIZE / SLAB_MIN_SIZE)

// One per page, kept outside the page.
struct slab_page {
    struct slab_page* next;     // class partial list, or the free-page pool
    struct slab_page* prev;     // class partial list only
    uintptr_t         addr;     // the page itself
    void*             free_list;
    uint16_t          cls;      // SLAB_NO_CLASS while in the free-page pool
    uint16_t          in_use;
    uint8_t           used[SLAB_MAX_PER_PAGE / 8];
};

struct slab_chunk {
    uintptr_t         base;
    size_t            pages;
    struct slab_page* desc;
};

struct slab_class {
    size_t            size;
    uint16_t          per_page;
    struct slab_page* partial;  // pages with at least one free object
};

static struct multiheap*  slab_backing = NULL;
static struct slab_chunk  slab_chunks[SLAB_MAX_CHUNKS];
static size_t             slab_chunk_count = 0;
static struct slab_class  slab_classes[SLAB_TOTAL_CLASSES];
static struct slab_page*  slab_free_pages = NULL;
static struct slab_stats  slab_stat;

void slab_init(struct multiheap* backing){
    slab_backing     = backing;
    slab_chunk_count = 0;
    slab_free_pages  = NULL;
    memset(&slab_stat, 0, sizeof(slab_stat));
    size_t size = SLAB_MIN_SIZE;
    for(int i = 0; i < SLAB_TOTAL_CLASSES; i++){
        slab_classes[i].size     = size;
        slab_classes[i].per_page = SLAB_PAGE_SIZE / size;
        slab_classes[i].partial  = NULL;
        size <<= 1;
    }
}

static int slab_class_for(size_t size){
    size_t s = SLAB_MIN_SIZE;
    int cls = 0;
    while(s < size){
        s <<= 1;
        cls++;
    }
    return cls;
}

static void slab_partial_push(struct slab_class* c, struct slab_page* p){
    p->prev = NULL;
    p->next = c->partial;
    if(c->partial){
        c->partial->prev = p;
    }
    c->partial = p;
}

static void slab_partial_remove(struct slab_class* c, struct slab_page* p){
    if(p->prev){
        p->prev->next = p->next;
    } else {
        c->partial = p->next;
    }
    if(p->next){
        p->next->prev = p->prev;
    }
    p->next = p->prev = NULL;
}

// Get another chunk of pages from the heap. Halves the chunk size
// until it fits, so a fragmented heap still yields slab pages.
static bool slab_grow(void){
    if(!slab_backing || slab_chunk_count >= SLAB_MAX_CHUNKS){
        return false;
    }
    for(size_t pages = SLAB_CHUNK_PAGES; pages >= SLAB_MIN_CHUNK_PAGES; pages /= 2){
        void* mem = multiheap_alloc(slab_backing, pages * SLAB_PAGE_SIZE);
        if(!mem){
            continue;
        }
        struct slab_page* desc = multiheap_alloc(slab_backing, pages * sizeof(struct slab_page));
        if(!desc){
            multiheap_free(slab_backing, mem);
            return false;
        }
        memset(desc, 0, pages * sizeof(struct slab_page));

        struct slab_chunk* chunk = &slab_chunks[slab_chunk_count++];
        chunk->base  = (uintptr_t)mem;
        chunk->pages = pages;
        chunk->desc  = desc;
        for(size_t i = pages; i-- > 0;){
            desc[i].addr = chunk->base + i * SLAB_PAGE_SIZE;
            desc[i].cls  = SLAB_NO_CLASS;
            desc[i].next = slab_free_pages;
            slab_free_pages = &desc[i];
        }
        slab_stat.chunks++;
        slab_stat.pages_total += pages;
        return true;
    }
    return false;
}

// Take a page from the pool and cut it into objects of class `cls`.
static struct slab_page* slab_page_take(int cls){
    if(!slab_free_pages && !slab_grow()){
        return NULL;
    }
    struct slab_page* p = slab_free_pages;
    slab_free_pages = p->next;

    struct slab_class* c = &slab_classes[cls];
    void* list = NULL;
    for(size_t k = c->per_page; k-- > 0;){
        void* obj = (void*)(p->addr + k * c->size);
        *(void**)obj = list;
        list = obj;
    }
    p->free_list = list;
    p->cls       = (uint16_t)cls;
    p->in_use    = 0;
    p->next = p->prev = NULL;
    memset(p->used, 0, sizeof(p->used));
    slab_stat.pages_in_use++;
    return p;
}

static void slab_page_release(struct slab_page* p){
    p->cls       = SLAB_NO_CLASS;
    p->free_list = NULL;
    p->prev      = NULL;
    p->next      = slab_free_pages;
    slab_free_pages = p;
    slab_stat.pages_in_use--;
}

void* slab_alloc(size_t size){
    if(size == 0 || size > SLAB_MAX_SIZE){
        return NULL;
    }
    int cls = slab_class_for(size);
    struct slab_class* c = &slab_classes[cls];

    struct slab_page* p = c->partial;
    if(!p){
        p = slab_page_take(cls);
        if(!p){
            return NULL;
        }
        slab_partial_push(c, p);
    }

    void* obj = p->free_list;
    p->free_list = *(void**)obj;
    size_t idx = ((uintptr_t)obj - p->addr) / c->size;
    p->used[idx / 8] |= (uint8_t)(1 << (idx % 8));
    p->in_use++;
    slab_stat.objects_in_use++;
    if(p->in_use == c->per_page){
        slab_partial_remove(c, p);
    }
    return obj;
}

static struct slab_page* slab_lookup(void* ptr){
    uintptr_t a = (uintptr_t)ptr;
    for(size_t i = 0; i < slab_chunk_count; i++){
        struct slab_chunk* chunk = &slab_chunks[i];
        if(a >= chunk->base && a < chunk->base + chunk->pages * SLAB_PAGE_SIZE){
            return &chunk->desc[(a - chunk->base) / SLAB_PAGE_SIZE];
        }
    }
    return NULL;
}

bool slab_owns(void* ptr){
    return slab_lookup(ptr) != NULL;
}

// The descriptor of the page holding the live object at `ptr`, and
// the object's index in it; NULL if `ptr` is not a live object.
static struct slab_page* slab_live_object(void* ptr, size_t* idx_out){
    struct slab_page* p = slab_lookup(ptr);
    if(!p || p->cls == SLAB_NO_CLASS){
        return NULL;
    }
    size_t size = slab_classes[p->cls].size;
    size_t off  = (uintptr_t)ptr - p->addr;
    if(off % size){
        return NULL;
    }
    size_t idx = off / size;
    if(!(p->used[idx / 8] & (1 << (idx % 8)))){
        return NULL;
    }
    *idx_out = idx;
    return p;
}

size_t slab_object_size(void* ptr){
    size_t idx;
    struct slab_page* p = slab_live_object(ptr, &idx);
    return p ? slab_classes[p->cls].size : 0;
}

int slab_free(void* ptr){
    size_t idx;
    struct slab_page* p = slab_live_object(ptr, &idx);
    if(!p){
        slab_stat.bad_frees++;
        return -EINVARG;
    }
    struct slab_class* c = &slab_classes[p->cls];
    bool was_full = p->in_use == c->per_page;

    p->used[idx / 8] &= (uint8_t)~(1 << (idx % 8));
    *(void**)ptr = p->free_list;
    p->free_list = ptr;
    p->in_use--;
    slab_stat.objects_in_use--;

    if(p->in_use == 0){
        if(!was_full){
            slab_partial_remove(c, p);
        }
        slab_page_release(p);
    } else if(was_full){
        slab_partial_push(c, p);
    }
    return 0;
}

void slab_get_stats(struct slab_stats* out){
    *out = slab_stat;
}
