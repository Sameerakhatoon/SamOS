#ifndef PFA_H
#define PFA_H

// Physical page-frame allocator.
//
// Owns every 4 KiB frame of usable RAM the firmware reported. It is
// built from the memory map at boot and replaces the book's "one
// E820 region over 100 MiB, bitmap at 0x01000000" minimal heap.
//
// The frame table is a struct heap over [lowest usable frame,
// highest usable frame): one byte per frame, the same TAKEN /
// IS_FIRST / HAS_NEXT encoding the heap uses, so a contiguous run
// of frames can be freed from its first address alone. Everything
// that is not usable RAM - holes between map entries, the kernel
// image, the boot stack, the memory map, the frame table itself - is
// marked HEAP_BLOCK_RESERVED and never handed out.
//
// Frames are handed out lowest address first. Until paging_switch
// the CPU runs on kernel.asm's boot tables, which identity-map only
// the first 1 GiB, so early allocations must stay below that.

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define PFA_FRAME_SIZE          4096
// Frames below 1 MiB (real-mode IVT/BDA, the boot-marker block at
// 0x6000, firmware leftovers) are never managed.
#define PFA_LOW_LIMIT           0x100000ULL
// kernel.asm's boot page tables cover only 0..1 GiB.
#define PFA_EARLY_MAP_LIMIT     0x40000000ULL

struct heap;

void         pfa_init(void);
struct heap* pfa_heap(void);

// Contiguous run of `count` frames, lowest address first, not
// zeroed. NULL when no run that long is free.
void*  pfa_alloc_frames(size_t count);
// Releases a run returned by pfa_alloc_frames. 0, or -EINVARG for
// anything that is not the start of a live run.
int    pfa_free_frames(void* addr);
bool   pfa_is_reserved(void* addr);

size_t pfa_total_frames(void);
size_t pfa_free_frame_count(void);
size_t pfa_used_frame_count(void);
size_t pfa_reserved_frame_count(void);

#endif
