// Page-frame allocator. See pfa.h.
#include "arch/aarch64/pfa.h"
#include "arch/aarch64/mmu.h"

enum { FRAME_FREE, FRAME_USED, FRAME_RESERVED };

static uint64_t base;           // pa of frame 0
static uint64_t frames;
static uint64_t table_pa, table_bytes;
static uint64_t nfree, nused;
static uint64_t next;           // next-fit cursor

static uint64_t up(uint64_t x)   { return (x + PFA_FRAME - 1) & ~(PFA_FRAME - 1); }
static uint64_t down(uint64_t x) { return x & ~(PFA_FRAME - 1); }

static uint8_t* table(void)
{
    return phys_to_virt(table_pa);
}

static int usable(const struct mem_region* r)
{
    return r->type == MEM_REGION_USABLE;
}

// Set every frame of [start, end) inside the span to `state`.
static void mark(uint64_t start, uint64_t end, uint8_t state)
{
    uint8_t* t = table();
    if (start < base)
        start = base;
    if (end > base + frames * PFA_FRAME)
        end = base + frames * PFA_FRAME;
    for (uint64_t a = start; a < end; a += PFA_FRAME)
        t[(a - base) / PFA_FRAME] = state;
}

// First frame-aligned spot of `bytes` in usable RAM that overlaps no
// other region (the memory manager code's pfa_find_table_home).
static uint64_t find_home(const struct mem_region* map, int n, uint64_t bytes)
{
    for (int i = 0; i < n; i++) {
        if (!usable(&map[i]))
            continue;
        uint64_t cand = up(map[i].base), end = down(map[i].base + map[i].length);
        for (int moved = 1; moved && cand + bytes <= end;) {
            moved = 0;
            for (int j = 0; j < n; j++) {
                uint64_t s = down(map[j].base), e = up(map[j].base + map[j].length);
                if (!usable(&map[j]) && cand < e && s < cand + bytes) {
                    cand = e;
                    moved = 1;
                }
            }
        }
        if (cand + bytes <= end)
            return cand;
    }
    return 0;
}

int pfa_init(const struct mem_region* map, int n)
{
    uint64_t lo = ~0ul, hi = 0;
    for (int i = 0; i < n; i++) {
        if (!usable(&map[i]))
            continue;
        if (up(map[i].base) < lo)
            lo = up(map[i].base);
        if (down(map[i].base + map[i].length) > hi)
            hi = down(map[i].base + map[i].length);
    }
    if (hi <= lo)
        return -1;
    base = lo;
    frames = (hi - lo) / PFA_FRAME;
    table_bytes = up(frames);
    table_pa = find_home(map, n, table_bytes);
    if (!table_pa)
        return -1;

    // Everything reserved, then usable RAM opened, then every other
    // region and the table closed again: overlaps resolve to reserved.
    mark(base, hi, FRAME_RESERVED);
    for (int i = 0; i < n; i++)
        if (usable(&map[i]))
            mark(up(map[i].base), down(map[i].base + map[i].length), FRAME_FREE);
    for (int i = 0; i < n; i++)
        if (!usable(&map[i]))
            mark(down(map[i].base), up(map[i].base + map[i].length), FRAME_RESERVED);
    mark(table_pa, table_pa + table_bytes, FRAME_RESERVED);

    uint8_t* t = table();
    nfree = nused = next = 0;
    for (uint64_t i = 0; i < frames; i++)
        nfree += t[i] == FRAME_FREE;
    return 0;
}

uint64_t pfa_alloc(void)
{
    uint8_t* t = table();
    for (uint64_t k = 0; nfree && k < frames; k++) {
        uint64_t i = (next + k) % frames;
        if (t[i] == FRAME_FREE) {
            t[i] = FRAME_USED;
            nfree--;
            nused++;
            next = i + 1;
            return base + i * PFA_FRAME;
        }
    }
    return 0;
}

int pfa_free(uint64_t pa)
{
    if (pa < base || (pa & (PFA_FRAME - 1)) || (pa - base) / PFA_FRAME >= frames)
        return -1;
    uint8_t* t = table();
    uint64_t i = (pa - base) / PFA_FRAME;
    if (t[i] != FRAME_USED)
        return -1;      // free already, or reserved
    t[i] = FRAME_FREE;
    nfree++;
    nused--;
    return 0;
}

uint64_t pfa_total_frames(void)    { return frames; }
uint64_t pfa_free_frames(void)     { return nfree; }
uint64_t pfa_used_frames(void)     { return nused; }
uint64_t pfa_reserved_frames(void) { return frames - nfree - nused; }
uint64_t pfa_table_pa(void)        { return table_pa; }
uint64_t pfa_table_bytes(void)     { return table_bytes; }
