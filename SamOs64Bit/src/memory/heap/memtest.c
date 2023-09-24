// Memory-manager self-tests. See memtest.h.
//
// Each test returns 1 on success or the __LINE__ of the first check
// that failed, and the result goes straight into its boot marker so
// a failing e2e run says which check broke.

#include "memtest.h"
#include "kheap.h"
#include "heap.h"
#include "slab.h"
#include "config.h"
#include "kernel.h"
#include "bootmarker.h"
#include "memory/memory.h"
#include "memory/pfa.h"
#include "memory/paging/paging.h"
#include "string/string.h"
#include "task/process.h"

#define BS SAMOS_HEAP_BLOCK_SIZE
#define CHECK(cond) do { if(!(cond)) { return __LINE__; } } while(0)

extern char samos_kernel_end[];

static bool memtest_stats_equal(struct kheap_stats* a, struct kheap_stats* b){
    return a->frames_free         == b->frames_free &&
           a->frames_used         == b->frames_used &&
           a->frames_reserved     == b->frames_reserved &&
           a->slab_chunks         == b->slab_chunks &&
           a->slab_pages_in_use   == b->slab_pages_in_use &&
           a->slab_objects_in_use == b->slab_objects_in_use &&
           a->bad_frees           == b->bad_frees;
}

// ---------------------------------------------------------------
// Frame allocator invariants.
// ---------------------------------------------------------------
static int memtest_pfa(void){
    struct heap* h = pfa_heap();
    CHECK(h->free_blocks + h->used_blocks + h->reserved_blocks == h->total_blocks);

    // Everything in use at kernel entry is reserved. Frames are
    // direct-map addresses, the kernel image is at SAMOS_KERNEL_VMA.
    CHECK(pfa_is_reserved(PAGING_PHYS_TO_VIRT(SAMOS_KERNEL_LOCATION)));
    CHECK(pfa_is_reserved(PAGING_PHYS_TO_VIRT(samos_kernel_end - 1 - SAMOS_KERNEL_VMA)));
    CHECK(pfa_is_reserved(PAGING_PHYS_TO_VIRT(SAMOS_BOOT_STACK_TOP - 8)));
    CHECK(pfa_is_reserved(PAGING_PHYS_TO_VIRT(SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION)));
    CHECK(pfa_is_reserved((void*)h->table->entries));
    CHECK(pfa_is_reserved((void*)(h->table->entries + h->table->total - 1)));
    CHECK(pfa_is_reserved(PAGING_PHYS_TO_VIRT(0x6000)));   // boot markers, below 1 MiB
    CHECK(PAGING_IS_DIRECT_MAP(h->saddr));

    // A reserved frame cannot be freed.
    CHECK(pfa_free_frames(PAGING_PHYS_TO_VIRT(SAMOS_KERNEL_LOCATION)) < 0);

    size_t before = pfa_free_frame_count();
    uint8_t* f = pfa_alloc_frames(4);
    CHECK(f && ((uintptr_t)f % PFA_FRAME_SIZE) == 0);
    CHECK(!pfa_is_reserved(f) && !pfa_is_reserved(f + 3 * PFA_FRAME_SIZE));
    CHECK(pfa_free_frame_count() == before - 4);
    f[0] = 0xA5;
    f[4 * PFA_FRAME_SIZE - 1] = 0x5A;
    CHECK(pfa_free_frames(f + PFA_FRAME_SIZE) < 0);   // not the start
    CHECK(pfa_free_frames(f) == 0);
    CHECK(pfa_free_frames(f) < 0);                    // double free
    CHECK(pfa_free_frame_count() == before);
    CHECK(pfa_alloc_frames(0) == NULL);
    CHECK(pfa_alloc_frames(h->total_blocks + 1) == NULL);
    return 1;
}

// ---------------------------------------------------------------
// heap.c on a private 16-block heap. entries[N] is a guard byte
// just past the table that the book's realloc grew into.
// ---------------------------------------------------------------
#define UNIT_BLOCKS 16
static HEAP_BLOCK_TABLE_ENTRY memtest_entries[UNIT_BLOCKS + 1];

static int memtest_heap_unit_body(uint8_t* pool){
    const size_t N = UNIT_BLOCKS;
    struct heap_table table = { memtest_entries, N };
    struct heap h;
    HEAP_BLOCK_TABLE_ENTRY* e = memtest_entries;
    CHECK(heap_create(&h, pool, pool + N * BS, &table) == 0);
    e[N] = HEAP_BLOCK_TABLE_ENTRY_FREE;

    // HAS_NEXT off-by-one: the last block of a run is a terminator,
    // so freeing a 2-block run leaves its neighbour alone.
    uint8_t* a = heap_malloc(&h, 2 * BS);
    uint8_t* b = heap_malloc(&h, BS);
    CHECK(a == pool && b == pool + 2 * BS);
    CHECK((e[0] & HEAP_BLOCK_HAS_NEXT) && !(e[1] & HEAP_BLOCK_HAS_NEXT));
    CHECK(heap_allocation_block_count(&h, a) == 2);
    CHECK(heap_free(&h, a) == 0);
    CHECK(heap_is_allocation_start(&h, b));
    CHECK(h.used_blocks == 1 && h.free_blocks == N - 1);
    CHECK(heap_free(&h, b) == 0 && h.free_blocks == N && h.used_blocks == 0);

    // Invalid frees are rejected and change nothing.
    a = heap_malloc(&h, 3 * BS);
    CHECK(a);
    CHECK(heap_free(&h, a + BS) < 0);          // middle of a run
    CHECK(heap_free(&h, a + 1) < 0);           // unaligned
    CHECK(heap_free(&h, pool + N * BS) < 0);   // one past the end
    CHECK(heap_free(&h, NULL) < 0);
    CHECK(h.used_blocks == 3 && heap_allocation_block_count(&h, a) == 3);
    CHECK(heap_free(&h, a) == 0);
    CHECK(heap_free(&h, a) < 0);               // double free
    CHECK(h.free_blocks == N && h.used_blocks == 0);

    // Realloc shrink counts the freed blocks once.
    a = heap_malloc(&h, 4 * BS);
    CHECK(heap_realloc(&h, a, BS) == a);
    CHECK(h.used_blocks == 1 && h.free_blocks == N - 1);
    CHECK(!(e[0] & HEAP_BLOCK_HAS_NEXT));
    // ... and grows back in place.
    CHECK(heap_realloc(&h, a, 3 * BS) == a);
    CHECK(h.used_blocks == 3 && heap_allocation_block_count(&h, a) == 3);
    CHECK(heap_free(&h, a) == 0 && h.free_blocks == N);

    // Growing the last block must not claim the guard past the table.
    a = heap_malloc(&h, (N - 1) * BS);
    b = heap_malloc(&h, BS);
    CHECK(a && b == pool + (N - 1) * BS);
    CHECK(heap_realloc(&h, b, 2 * BS) == NULL);
    CHECK(e[N] == HEAP_BLOCK_TABLE_ENTRY_FREE);
    CHECK(heap_allocation_block_count(&h, b) == 1 && h.free_blocks == 0);
    CHECK(heap_free(&h, a) == 0 && heap_free(&h, b) == 0 && h.free_blocks == N);

    // Realloc that has to move keeps the contents.
    a = heap_malloc(&h, BS);
    b = heap_malloc(&h, BS);
    memset(a, 0x5A, BS);
    uint8_t* r = heap_realloc(&h, a, 2 * BS);
    CHECK(r && r != a && r[0] == 0x5A && r[BS - 1] == 0x5A && r[BS] == 0);
    CHECK(!heap_is_allocation_start(&h, a));
    CHECK(heap_realloc(&h, pool + 7 * BS, BS) == NULL);   // not an allocation
    CHECK(heap_free(&h, r) == 0 && heap_free(&h, b) == 0 && h.free_blocks == N);

    // Oversized requests fail without wrapping.
    CHECK(heap_malloc(&h, ~(size_t)0) == NULL);
    CHECK(heap_malloc(&h, 0) == NULL);

    // Reserve rounds outward, unreserve inward.
    heap_reserve_range(&h, (uintptr_t)pool + BS + 1, (uintptr_t)pool + 2 * BS + 1);
    CHECK(h.reserved_blocks == 2 && h.free_blocks == N - 2);
    CHECK(heap_free(&h, pool + BS) < 0);
    a = heap_malloc(&h, 2 * BS);
    CHECK(a == pool + 3 * BS);
    CHECK(heap_free(&h, a) == 0);
    heap_unreserve_range(&h, (uintptr_t)pool + BS + 1, (uintptr_t)pool + 3 * BS);
    CHECK(h.reserved_blocks == 1 && heap_is_block_reserved(&h, pool + BS));
    heap_unreserve_range(&h, (uintptr_t)pool, (uintptr_t)pool + N * BS);
    CHECK(h.reserved_blocks == 0 && h.free_blocks == N);
    return 1;
}

static int memtest_heap_unit(void){
    uint8_t* pool = pfa_alloc_frames(UNIT_BLOCKS);
    CHECK(pool);
    int res = memtest_heap_unit_body(pool);
    CHECK(pfa_free_frames(pool) == 0);
    return res;
}

// ---------------------------------------------------------------
// Slab layer through kmalloc / kfree / krealloc.
// ---------------------------------------------------------------
#define SLAB_BATCH 64
static void* memtest_ptrs[SLAB_BATCH];

static int memtest_slab(void){
    struct kheap_stats base, now;

    // Warm-up so the pages the batch below needs are already in the
    // slab pool and the frame counts stay put.
    for(int i = 0; i < SLAB_BATCH; i++){
        memtest_ptrs[i] = kmalloc(50);
    }
    for(int i = 0; i < SLAB_BATCH; i++){
        kfree(memtest_ptrs[i]);
    }
    kheap_get_stats(&base);

    uint8_t* p = kmalloc(50);
    CHECK(p && slab_owns(p));
    CHECK(((uintptr_t)p % 64) == 0 && slab_object_size(p) == 64);
    CHECK(slab_object_size(NULL) == 0);

    // 64 objects of 64 B fill one page: at most two new pages.
    for(int i = 0; i < SLAB_BATCH; i++){
        memtest_ptrs[i] = kmalloc(50);
        CHECK(memtest_ptrs[i]);
    }
    kheap_get_stats(&now);
    CHECK(now.slab_pages_in_use <= base.slab_pages_in_use + 2);
    CHECK(now.frames_used == base.frames_used);   // pages come from existing chunks
    for(int i = 0; i < SLAB_BATCH; i++){
        kfree(memtest_ptrs[i]);
    }

    // Class boundaries.
    void* c32   = kmalloc(32);
    void* c33   = kmalloc(33);
    void* c2048 = kmalloc(2048);
    void* c2049 = kmalloc(2049);
    CHECK(c32 && slab_object_size(c32) == 32);
    CHECK(c33 && slab_object_size(c33) == 64);
    CHECK(c2048 && slab_object_size(c2048) == 2048);
    CHECK(c2049 && !slab_owns(c2049) && ((uintptr_t)c2049 % BS) == 0);
    kfree(c32); kfree(c33); kfree(c2048); kfree(c2049);

    // Bad frees are rejected and counted; the object survives.
    size_t bad = base.bad_frees;
    p[0] = 0x77;
    kfree(p + 1);
    CHECK(slab_object_size(p) == 64);
    kheap_get_stats(&now);
    CHECK(now.bad_frees == bad + 1);

    // krealloc: stays within the class, moves beyond it.
    CHECK(krealloc(p, 60) == p);
    uint8_t* q = krealloc(p, 3000);
    CHECK(q && q != p && q[0] == 0x77 && !slab_owns(q));
    CHECK(slab_object_size(p) == 0);
    kfree(p);                                   // already freed by krealloc
    kheap_get_stats(&now);
    CHECK(now.bad_frees == bad + 2);
    kfree(q);

    kheap_get_stats(&now);
    CHECK(now.slab_objects_in_use == base.slab_objects_in_use);
    CHECK(now.frames_used == base.frames_used);
    return 1;
}

// ---------------------------------------------------------------
// 1000 allocate/free cycles must leave the allocator exactly where
// it started. Each cycle mixes slab and frame allocations, krealloc
// across the slab/frame boundary, and a page-table tree built and
// torn down by paging.c (whose frees used to be no-ops).
// ---------------------------------------------------------------
#define STEADY_CYCLES 1000
static const size_t memtest_steady_sizes[] = {
    24, 50, 100, 200, 512, 1000, 2048, 3000, 4096, 8192, 20000, 65536,
};
#define STEADY_SLOTS (sizeof(memtest_steady_sizes) / sizeof(memtest_steady_sizes[0]))

static int memtest_steady_cycle(void){
    void* p[STEADY_SLOTS];
    for(size_t i = 0; i < STEADY_SLOTS; i++){
        // kzalloc only on a few slots: memset runs byte by byte at
        // -O0 under TCG, and 1000 cycles of 64 KiB adds up.
        bool zero = (i == 1 || i == 5 || i == 8);
        p[i] = zero ? kzalloc(memtest_steady_sizes[i]) : kmalloc(memtest_steady_sizes[i]);
        CHECK(p[i]);
        ((uint8_t*)p[i])[0] = (uint8_t)i;
        ((uint8_t*)p[i])[memtest_steady_sizes[i] - 1] = (uint8_t)i;
    }
    p[1] = krealloc(p[1], 300);      // slab -> bigger slab class
    CHECK(p[1] && ((uint8_t*)p[1])[0] == 1);
    p[3] = krealloc(p[3], 9000);     // slab -> frames
    CHECK(p[3] && ((uint8_t*)p[3])[0] == 3);
    p[10] = krealloc(p[10], 5000);   // frames shrink
    CHECK(p[10] && ((uint8_t*)p[10])[0] == 10);

    // Page tables take physical addresses; the descriptor
    // shares the kernel half, which paging_desc_free must leave alone.
    struct paging_desc* d = paging_desc_new(PAGING_MAP_LEVEL_4);
    CHECK(d);
    uint64_t phys9 = paging_kernel_virt_to_phys(p[9]);
    CHECK(paging_map(d, (void*)0x400000, (void*)phys9, PAGING_IS_PRESENT | PAGING_IS_WRITEABLE) == 0);
    CHECK(paging_map(d, (void*)0x401000, (void*)(phys9 + BS), PAGING_IS_PRESENT) == 0);
    CHECK(paging_map(d, (void*)0x7F0000000000ULL, (void*)paging_kernel_virt_to_phys(p[8]),
                     PAGING_IS_PRESENT) == 0);
    CHECK(paging_get_physical_address(d, (void*)0x401000) == (void*)(phys9 + BS));
    CHECK(paging_map(d, (void*)SAMOS_DIRECT_MAP_BASE, NULL, PAGING_IS_PRESENT | PAGING_ACCESS_FROM_ALL) < 0);
    CHECK(d->pml->entries[PAGING_KERNEL_PML4_FIRST].address ==
          kernel_desc()->pml->entries[PAGING_KERNEL_PML4_FIRST].address);
    paging_desc_free(d);

    for(size_t i = 0; i < STEADY_SLOTS; i++){
        kfree(p[i]);
    }
    return 1;
}

static int memtest_steady(void){
    struct kheap_stats base, now;
    int res = memtest_steady_cycle();    // warm-up: may grow slab chunks
    if(res != 1){
        return res;
    }
    kheap_get_stats(&base);
    for(int c = 0; c < STEADY_CYCLES; c++){
        res = memtest_steady_cycle();
        if(res != 1){
            return res;
        }
    }
    kheap_get_stats(&now);
    CHECK(memtest_stats_equal(&base, &now));
    return 1;
}

// ---------------------------------------------------------------
// Randomised stress: 128 slots, alloc / realloc / free in a
// deterministic LCG order, sizes 1 B to 64 KiB. Each live block
// carries a tag pattern at both ends that must survive until it is
// freed, so overlapping allocations or a bad realloc copy show up.
// The run is done twice with the same seed; the second must leave
// the allocator exactly as the first did.
// ---------------------------------------------------------------
#define STRESS_SLOTS 128
#define STRESS_OPS   6000
#define STRESS_EDGE  32

struct memtest_slot {
    uint8_t* ptr;
    size_t   size;
    uint8_t  tag;
};
static struct memtest_slot memtest_slots[STRESS_SLOTS];
static uint32_t memtest_rng;

static uint32_t memtest_rand(void){
    memtest_rng = memtest_rng * 1664525u + 1013904223u;
    return memtest_rng >> 8;
}

static size_t memtest_rand_size(void){
    uint32_t r = memtest_rand() % 100;
    if(r < 70) return 1 + memtest_rand() % 256;
    if(r < 90) return 257 + memtest_rand() % (4096 - 256);
    return 4097 + memtest_rand() % (65536 - 4096);
}

static void memtest_tag(struct memtest_slot* s){
    size_t n = s->size < STRESS_EDGE ? s->size : STRESS_EDGE;
    memset(s->ptr, s->tag, n);
    memset(s->ptr + s->size - n, s->tag, n);
}

// Checks the first `len` bytes' edge and, if whole, the tail edge.
static bool memtest_tag_ok(struct memtest_slot* s, size_t len, bool tail){
    size_t n = len < STRESS_EDGE ? len : STRESS_EDGE;
    for(size_t i = 0; i < n; i++){
        if(s->ptr[i] != s->tag) return false;
    }
    if(tail){
        for(size_t i = s->size - n; i < s->size; i++){
            if(s->ptr[i] != s->tag) return false;
        }
    }
    return true;
}

static int memtest_stress_run(void){
    memtest_rng = 0x5EED1234u;
    memset(memtest_slots, 0, sizeof(memtest_slots));
    uint8_t next_tag = 1;

    for(int op = 0; op < STRESS_OPS; op++){
        struct memtest_slot* s = &memtest_slots[memtest_rand() % STRESS_SLOTS];
        if(!s->ptr){
            s->size = memtest_rand_size();
            s->ptr  = kmalloc(s->size);
            CHECK(s->ptr);
            s->tag  = next_tag++;
            if(!next_tag) next_tag = 1;
            memtest_tag(s);
            continue;
        }
        CHECK(memtest_tag_ok(s, s->size, true));
        if(memtest_rand() % 4 == 0){
            size_t new_size = memtest_rand_size();
            size_t keep     = new_size < s->size ? new_size : s->size;
            uint8_t* n = krealloc(s->ptr, new_size);
            CHECK(n);
            s->ptr = n;
            CHECK(memtest_tag_ok(s, keep, false));
            s->size = new_size;
            memtest_tag(s);
        } else {
            kfree(s->ptr);
            s->ptr = NULL;
        }
    }
    for(int i = 0; i < STRESS_SLOTS; i++){
        struct memtest_slot* s = &memtest_slots[i];
        if(s->ptr){
            CHECK(memtest_tag_ok(s, s->size, true));
            kfree(s->ptr);
            s->ptr = NULL;
        }
    }
    return 1;
}

static int memtest_stress(void){
    struct kheap_stats base, first, second;
    kheap_get_stats(&base);
    int res = memtest_stress_run();
    if(res != 1){
        return res;
    }
    kheap_get_stats(&first);
    CHECK(first.slab_objects_in_use == base.slab_objects_in_use);
    CHECK(first.bad_frees == base.bad_frees);
    res = memtest_stress_run();
    if(res != 1){
        return res;
    }
    kheap_get_stats(&second);
    CHECK(memtest_stats_equal(&first, &second));
    return 1;
}

// ---------------------------------------------------------------
// Process create/exit cycles: load BLANK.ELF off disk, build its
// task and page tables, terminate it. The process code still leaks
// a few slab objects per cycle (fopen never frees the parsed
// path, process_terminate never frees struct process), so this does
// not gate on a steady heap yet. It checks that frames and bad frees
// stay put and that the leak is the same every cycle, and stores the
// objects leaked per cycle in BM_FEATURE_MEM_PROCESS_LEAK for the
// e2e test to bound. Each cycle identity-maps all of RAM with 4 KiB
// pages, so the count stays small.
// ---------------------------------------------------------------
#define PROCESS_CYCLES 8

static int memtest_process_cycle(void){
    struct process* proc = 0;
    CHECK(process_load("@:/BLANK.ELF", &proc) == 0);
    CHECK(proc);
    CHECK(process_terminate(proc) == 0);
    return 1;
}

static int memtest_process(void){
    struct kheap_stats base, now;
    int res = memtest_process_cycle();   // warm-up: streamer cache, slab pages
    if(res != 1){
        return res;
    }
    kheap_get_stats(&base);
    for(int c = 0; c < PROCESS_CYCLES; c++){
        res = memtest_process_cycle();
        if(res != 1){
            return res;
        }
    }
    kheap_get_stats(&now);
    CHECK(now.frames_used == base.frames_used);
    CHECK(now.bad_frees == base.bad_frees);
    size_t leaked = now.slab_objects_in_use - base.slab_objects_in_use;
    CHECK(leaked % PROCESS_CYCLES == 0);
    boot_marker_set(BM_FEATURE_MEM_PROCESS_LEAK, (uint32_t)(leaked / PROCESS_CYCLES));
    return 1;
}

static void memtest_report(boot_marker_stage_t stage, const char* name, int res){
    boot_marker_set(stage, (uint32_t)res);
    print("memtest ");
    print(name);
    if(res == 1){
        print(": ok\n");
    } else {
        print(": FAILED at memtest.c:");
        print(itoa(res));
        print("\n");
    }
}

void memtest_run(void){
    struct kheap_stats s;
    kheap_get_stats(&s);
    boot_marker_set(BM_FEATURE_MEM_RAM_MIB,
                    (uint32_t)((s.frames_free + s.frames_used) / (1024 * 1024 / PFA_FRAME_SIZE)));

    memtest_report(BM_FEATURE_MEM_PFA,       "pfa",       memtest_pfa());
    memtest_report(BM_FEATURE_MEM_HEAP_UNIT, "heap unit", memtest_heap_unit());
    memtest_report(BM_FEATURE_MEM_SLAB,      "slab",      memtest_slab());
    memtest_report(BM_FEATURE_MEM_STEADY,    "steady",    memtest_steady());
    memtest_report(BM_FEATURE_MEM_STRESS,    "stress",    memtest_stress());
    memtest_report(BM_FEATURE_MEM_PROCESS,   "process",   memtest_process());
}
