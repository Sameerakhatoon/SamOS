// Page-frame allocator over the direct map. See pfa.h.

#include "arch/riscv64/pfa.h"
#include "arch/riscv64/mmu.h"

#define F_TAKEN    0x01
#define F_FIRST    0x02    // first frame of a run
#define F_HAS_NEXT 0x04    // the run goes on in the next frame
#define F_RESERVED 0x80

static uint8_t* table;          // one byte per frame, through the direct map
static uint64_t base_pa, nframes;
static uint64_t hint;           // next-fit: scans start after the last run
static size_t free_count, reserved_count;

static uint64_t align_down(uint64_t v) { return v & ~(uint64_t)(PFA_FRAME_SIZE - 1); }
static uint64_t align_up(uint64_t v) { return align_down(v + PFA_FRAME_SIZE - 1); }

static bool overlaps(uint64_t a, uint64_t alen, uint64_t b, uint64_t blen)
{
    return a < b + blen && b < a + alen;
}

// The first frame-aligned place for `size` bytes inside a usable region
// that no other region overlaps.
static uint64_t place_table(const struct mem_region* r, int count, uint64_t size)
{
    for (int i = 0; i < count; i++) {
        if (r[i].type != MEM_REGION_USABLE) {
            continue;
        }
        uint64_t at = align_up(r[i].base), end = r[i].base + r[i].length;
        for (int j = 0; j < count && at + size <= end; j++) {
            if (r[j].type != MEM_REGION_USABLE && overlaps(at, size, r[j].base, r[j].length)) {
                at = align_up(r[j].base + r[j].length);
                j = -1;     // rescan: the move may hit an earlier one
            }
        }
        if (at + size <= end) {
            return at;
        }
    }
    return 0;
}

// Set every frame touching [base, base + len) to `v`; reserved ranges
// round outward, usable ones inward.
static void mark(uint64_t base, uint64_t len, uint8_t v)
{
    uint64_t lo = v == F_RESERVED ? align_down(base) : align_up(base);
    uint64_t hi = v == F_RESERVED ? align_up(base + len) : align_down(base + len);
    lo = lo < base_pa ? base_pa : lo;
    for (uint64_t pa = lo; pa < hi && pa < base_pa + nframes * PFA_FRAME_SIZE; pa += PFA_FRAME_SIZE) {
        table[(pa - base_pa) / PFA_FRAME_SIZE] = v;
    }
}

int pfa_init(const struct mem_region* r, int count)
{
    uint64_t lo = ~0ul, hi = 0;
    for (int i = 0; i < count; i++) {
        if (r[i].type == MEM_REGION_USABLE) {
            lo = r[i].base < lo ? r[i].base : lo;
            hi = r[i].base + r[i].length > hi ? r[i].base + r[i].length : hi;
        }
    }
    if (hi <= lo) {
        return -1;
    }
    base_pa = align_down(lo);
    nframes = (align_down(hi) - base_pa) / PFA_FRAME_SIZE;
    uint64_t at = place_table(r, count, nframes);
    if (!at) {
        return -1;
    }
    table = phys_to_virt(at);
    for (uint64_t i = 0; i < nframes; i++) {
        table[i] = F_RESERVED;
    }
    for (int i = 0; i < count; i++) {
        if (r[i].type == MEM_REGION_USABLE) {
            mark(r[i].base, r[i].length, 0);
        }
    }
    for (int i = 0; i < count; i++) {
        if (r[i].type != MEM_REGION_USABLE) {
            mark(r[i].base, r[i].length, F_RESERVED);
        }
    }
    mark(at, nframes, F_RESERVED);
    free_count = reserved_count = 0;
    for (uint64_t i = 0; i < nframes; i++) {
        free_count += table[i] == 0;
        reserved_count += table[i] == F_RESERVED;
    }
    return 0;
}

bool pfa_ready(void)
{
    return table != 0;
}

// Next fit, wrapping once.
void* pfa_alloc_frames(size_t count)
{
    if (!table || count == 0 || count > free_count) {
        return 0;
    }
    for (uint64_t scanned = 0, i = hint; scanned < nframes; ) {
        if (i + count > nframes) {
            scanned += nframes - i;
            i = 0;
            continue;
        }
        size_t run = 0;
        while (run < count && table[i + run] == 0) {
            run++;
        }
        if (run == count) {
            for (size_t k = 0; k < count; k++) {
                table[i + k] = F_TAKEN | (k == 0 ? F_FIRST : 0) | (k + 1 < count ? F_HAS_NEXT : 0);
            }
            free_count -= count;
            hint = i + count;
            return phys_to_virt(base_pa + i * PFA_FRAME_SIZE);
        }
        scanned += run + 1;
        i += run + 1;
    }
    return 0;
}

int pfa_free_frames(void* addr)
{
    uint64_t pa = virt_to_phys(addr);
    if (!table || pa < base_pa || pa & (PFA_FRAME_SIZE - 1)) {
        return -1;
    }
    uint64_t i = (pa - base_pa) / PFA_FRAME_SIZE;
    if (i >= nframes || !(table[i] & F_TAKEN) || !(table[i] & F_FIRST)) {
        return -1;
    }
    for (;;) {
        uint8_t f = table[i];
        table[i++] = 0;
        free_count++;
        if (!(f & F_HAS_NEXT)) {
            return 0;
        }
    }
}

bool pfa_is_reserved(uint64_t pa)
{
    if (!table || pa < base_pa || pa >= base_pa + nframes * PFA_FRAME_SIZE) {
        return true;
    }
    return table[(pa - base_pa) / PFA_FRAME_SIZE] == F_RESERVED;
}

size_t pfa_total_frames(void) { return nframes; }
size_t pfa_free_frame_count(void) { return free_count; }
size_t pfa_reserved_frame_count(void) { return reserved_count; }
size_t pfa_used_frame_count(void) { return nframes - free_count - reserved_count; }
uint64_t pfa_table_pa(void) { return table ? virt_to_phys(table) : 0; }
uint64_t pfa_table_size(void) { return nframes; }
