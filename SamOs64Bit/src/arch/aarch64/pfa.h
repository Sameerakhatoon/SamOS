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
//
// The boot allocator only: it feeds mmu_early's tables. Once
// kernel_main's kheap_init has built the shared PFA (memory/pfa.c)
// from boot_info's memory map, where the frames used here are
// reserved (early_pfa_used_runs), mmu.c takes its frames from that one.

#define PFA_FRAME 4096ul

// 0, or -1 when there is no usable RAM or no room for the table.
int      early_pfa_init(const struct mem_region* map, int n);
uint64_t early_pfa_alloc(void);         // pa of a frame (not zeroed), 0 if none
int      early_pfa_free(uint64_t pa);   // 0, or -1 if pa is not an allocated frame
// Used frames and the table as MEM_REGION_KERNEL runs; -1 if more than max.
int      early_pfa_used_runs(struct mem_region* out, int max);

uint64_t early_pfa_total_frames(void);  // frames the table spans
uint64_t early_pfa_free_frames(void);
uint64_t early_pfa_used_frames(void);
uint64_t early_pfa_reserved_frames(void);
uint64_t early_pfa_table_pa(void);
uint64_t early_pfa_table_bytes(void);

#endif
