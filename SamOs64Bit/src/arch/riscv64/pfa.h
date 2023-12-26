#ifndef SAMOS_RISCV64_PFA_H
#define SAMOS_RISCV64_PFA_H

// Page-frame allocator, built from a struct mem_region list
// (x86's memregion.h) as the memory manager code's is: one byte per 4 KiB frame over the
// span of the usable regions, TAKEN / FIRST / HAS_NEXT so a contiguous
// run frees from its first address alone. Everything not usable (holes,
// every reservation, the frame table itself) is reserved and never
// handed out. Frames come back as direct-map addresses, as on x86.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "memory/memregion.h"

#define PFA_FRAME_SIZE 4096

// USABLE regions give frames; any other type, wherever it overlaps a
// usable one, takes them away. 0 or -1 (no room for the frame table).
int    pfa_init(const struct mem_region* regions, int count);
bool   pfa_ready(void);

// `count` contiguous frames, not zeroed; NULL when no run is free.
void*  pfa_alloc_frames(size_t count);
// 0, or -1 for anything that is not the start of a live run.
int    pfa_free_frames(void* addr);
bool   pfa_is_reserved(uint64_t pa);

size_t pfa_total_frames(void);
size_t pfa_free_frame_count(void);
size_t pfa_used_frame_count(void);
size_t pfa_reserved_frame_count(void);
// Where the frame table lives (physical) and its size in bytes.
uint64_t pfa_table_pa(void);
uint64_t pfa_table_size(void);

#endif
