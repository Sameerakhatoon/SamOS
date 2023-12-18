#ifndef ARCH_AARCH64_PFA_H
#define ARCH_AARCH64_PFA_H

#include <stdint.h>
#include "memory/memregion.h"

// Physical page-frame allocator, fed by an arch-neutral
// struct mem_region list (memmap.c builds it from the device tree).
//
// One byte per 4 KiB frame over [lowest usable, highest usable),
// like the x86 PFA: FREE, USED (handed out) or RESERVED (holes,
// every non-usable region, the table itself). The table lives in the
// first usable run that overlaps no reservation and is reached
// through phys_to_virt, so it works before and after the MMU is on.
// Nothing here is aarch64 specific beyond that include.

#define PFA_FRAME 4096ul

// 0, or -1 when there is no usable RAM or no room for the table.
int      pfa_init(const struct mem_region* map, int n);
uint64_t pfa_alloc(void);           // pa of a frame (not zeroed), 0 if none
int      pfa_free(uint64_t pa);     // 0, or -1 if pa is not an allocated frame

uint64_t pfa_total_frames(void);    // frames the table spans
uint64_t pfa_free_frames(void);
uint64_t pfa_used_frames(void);
uint64_t pfa_reserved_frames(void);
uint64_t pfa_table_pa(void);
uint64_t pfa_table_bytes(void);

#endif
